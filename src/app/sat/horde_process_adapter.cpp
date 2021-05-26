
#include <assert.h>
#include <sys/types.h>
#include <stdlib.h>

#include "horde_process_adapter.hpp"

#include "hordesat/horde.hpp"
#include "util/sys/shared_memory.hpp"
#include "util/sys/proc.hpp"
#include "util/sys/timer.hpp"
#include "util/sys/process.hpp"
#include "util/logger.hpp"

HordeProcessAdapter::HordeProcessAdapter(const Parameters& params, 
    size_t fSize, const int* fLits, size_t aSize, const int* aLits) :    
        _params(params), _f_size(fSize), _f_lits(fLits), _a_size(aSize), _a_lits(aLits) {

    initSharedMemory();
}

void HordeProcessAdapter::initSharedMemory() {

    // Initialize "management" shared memory
    _shmem_id = "/edu.kit.iti.mallob." + std::to_string(Proc::getPid()) + "." + _params["mpirank"] + ".#" + _params["jobid"];
    //log(V4_VVER, "Setup base shmem: %s\n", _shmem_id.c_str());
    void* mainShmem = SharedMemory::create(_shmem_id, sizeof(HordeSharedMemory));
    _shmem.insert(ShmemObject{_shmem_id, mainShmem, sizeof(HordeSharedMemory)});
    _hsm = new ((char*)mainShmem) HordeSharedMemory();
    _hsm->doExport = false;
    _hsm->doImport = false;
    _hsm->doDumpStats = false;
    _hsm->doStartNextRevision = false;
    _hsm->doTerminate = false;
    _hsm->exportBufferMaxSize = 0;
    _hsm->importBufferSize = 0;
    _hsm->didExport = false;
    _hsm->didImport = false;
    _hsm->didDumpStats = false;
    _hsm->didStartNextRevision = false;
    _hsm->didTerminate = false;
    _hsm->isSpawned = false;
    _hsm->isInitialized = false;
    _hsm->hasSolution = false;
    _hsm->result = UNKNOWN;
    _hsm->solutionRevision = -1;
    _hsm->exportBufferTrueSize = 0;
    _hsm->fSize = _f_size;
    _hsm->aSize = _a_size;
    _hsm->revision = _params.getIntParam("firstrev");

    createSharedMemoryBlock("formulae.0", sizeof(int) * _f_size, (void*)_f_lits);
    createSharedMemoryBlock("assumptions.0", sizeof(int) * _a_size, (void*)_a_lits);
    _export_buffer = (int*) createSharedMemoryBlock("clauseexport", 
            _params.getIntParam("cbbs") * sizeof(int), nullptr);
    _import_buffer = (int*) createSharedMemoryBlock("clauseimport", 
            _params.getIntParam("cbbs") * sizeof(int) * _params.getIntParam("mpisize"), nullptr);
}

HordeProcessAdapter::~HordeProcessAdapter() {
    freeSharedMemory();
}

void HordeProcessAdapter::freeSharedMemory() {
    if (_hsm != nullptr) {
        for (int rev = 0; rev <= _hsm->revision; rev++) {
            size_t* solSize = (size_t*) SharedMemory::access(_shmem_id + ".solutionsize." + std::to_string(rev), sizeof(size_t));
            if (solSize != nullptr) {
                char* solution = (char*) SharedMemory::access(_shmem_id + ".solution." + std::to_string(rev), *solSize * sizeof(int));
                SharedMemory::free(_shmem_id + ".solution." + std::to_string(rev), solution, *solSize * sizeof(int));
                SharedMemory::free(_shmem_id + ".solutionsize." + std::to_string(rev), (char*)solSize, sizeof(size_t));
            }
        }
        _hsm = nullptr;
    }
    for (auto& shmemObj : _shmem) {
        SharedMemory::free(shmemObj.id, (char*)shmemObj.data, shmemObj.size);
    }
    _shmem.clear();
}

pid_t HordeProcessAdapter::run() {

    // Assemble c-style program arguments
    const char* execName = "mallob_sat_process";
    char* const* argv = _params.asCArgs(execName);
    
    // FORK: Create a child process
    pid_t res = Process::createChild();
    if (res > 0) {
        // [parent process]
        _child_pid = res;
        //while (!_hsm->isSpawned) usleep(250 /* 1/4 milliseconds */);
        _state = SolvingStates::ACTIVE;

        int i = 1;
        while (argv[i] != nullptr) free(argv[i++]);
        delete[] argv;

        
        return res;
    }

    // [child process]
    // Execute the SAT process.
    int result = execvp("mallob_sat_process", argv);
    
    // If this is reached, something went wrong with execvp
    log(V0_CRIT, "execvp returned %i with errno %i\n", result, (int)errno);
    abort();
}

bool HordeProcessAdapter::isFullyInitialized() {
    return _hsm->isInitialized;
}

pid_t HordeProcessAdapter::getPid() {
    return _child_pid;
}

void HordeProcessAdapter::appendRevisions(const std::vector<RevisionData>& revisions) {

    int latestRevision = 0;
    for (size_t i = 0; i < revisions.size(); i++) {
        const auto& revData = revisions[i];
        latestRevision = std::max(latestRevision, revData.revision);
        auto revStr = std::to_string(revData.revision);
        createSharedMemoryBlock("fsize."       + revStr, sizeof(size_t),              (void*)&revData.fSize);
        createSharedMemoryBlock("asize."       + revStr, sizeof(size_t),              (void*)&revData.aSize);
        createSharedMemoryBlock("formulae."    + revStr, sizeof(int) * revData.fSize, (void*)revData.fLits);
        createSharedMemoryBlock("assumptions." + revStr, sizeof(int) * revData.aSize, (void*)revData.aLits);
        createSharedMemoryBlock("checksum."    + revStr, sizeof(Checksum),            (void*)&(revData.checksum));
    }

    _hsm->hasSolution = false;

    if (_state == SolvingStates::INITIALIZING) {
        // Child process not set up yet: Can directly order to parse new clauses
        _hsm->revision = latestRevision;
    } else {
        _revision_update = latestRevision;
    }
}

void HordeProcessAdapter::setSolvingState(SolvingStates::SolvingState state) {
    if (state == _state) return;

    if (state == SolvingStates::ABORTING) {
        //Fork::terminate(_child_pid); // Terminate child process by signal.
        _hsm->doTerminate = true; // Kindly ask child process to terminate.
        Process::resume(_child_pid); // Continue (resume) process.
    }
    if (state == SolvingStates::SUSPENDED) {
        Process::suspend(_child_pid); // Stop (suspend) process.
    }
    if (state == SolvingStates::ACTIVE) {
        _hsm->hasSolution = false;
        Process::resume(_child_pid); // Continue (resume) process.
    }

    _state = state;
}

void HordeProcessAdapter::collectClauses(int maxSize) {
    _hsm->exportBufferMaxSize = maxSize;
    _hsm->doExport = true;
    if (_hsm->isInitialized) Process::wakeUp(_child_pid);
}
bool HordeProcessAdapter::hasCollectedClauses() {
    return _hsm->didExport;
}
std::vector<int> HordeProcessAdapter::getCollectedClauses(Checksum& checksum) {
    if (!_hsm->didExport) {
        return std::vector<int>();
    }
    std::vector<int> clauses(_hsm->exportBufferTrueSize);
    memcpy(clauses.data(), _export_buffer, clauses.size()*sizeof(int));
    checksum = _hsm->exportChecksum;
    _hsm->doExport = false;
    return clauses;
}

void HordeProcessAdapter::digestClauses(const std::vector<int>& clauses, const Checksum& checksum) {
    if (_hsm->doImport && !_hsm->didImport) {
        log(V1_WARN, "Still digesting previous batch of clauses: discard this batch\n");
        return;
    }
    _hsm->importChecksum = checksum;
    _hsm->importBufferSize = clauses.size();
    memcpy(_import_buffer, clauses.data(), clauses.size()*sizeof(int));
    _hsm->doImport = true;
    if (_hsm->isInitialized) Process::wakeUp(_child_pid);
}

void HordeProcessAdapter::dumpStats() {
    _hsm->doDumpStats = true;
    // No hard need to wake up immediately
}

bool HordeProcessAdapter::check() {

    if (_hsm->didImport)            _hsm->doImport            = false;
    if (_hsm->didStartNextRevision) _hsm->doStartNextRevision = false;
    if (_hsm->didDumpStats)         _hsm->doDumpStats         = false;

    if (!_hsm->doStartNextRevision 
        && !_hsm->didStartNextRevision 
        && _revision_update >= 0) {
        
        _hsm->revision = _revision_update;
        _revision_update = -1;
        _hsm->doStartNextRevision = true;
    }
    
    if (_hsm->hasSolution) return _hsm->solutionRevision == _hsm->revision;
    return false;
}

std::pair<SatResult, std::vector<int>> HordeProcessAdapter::getSolution() {

    int rev = _hsm->solutionRevision;
    size_t* solutionSize = (size_t*) SharedMemory::access(_shmem_id + ".solutionsize." + std::to_string(rev), sizeof(size_t));
    if (*solutionSize == 0) return std::pair<SatResult, std::vector<int>>(_hsm->result, std::vector<int>()); 

    std::vector<int> solution(*solutionSize);

    // ACCESS the existing shared memory segment to the solution vector
    int* shmemSolution = (int*) SharedMemory::access(_shmem_id + ".solution." + std::to_string(rev), solution.size()*sizeof(int));
    memcpy(solution.data(), shmemSolution, solution.size()*sizeof(int));
    
    return std::pair<SatResult, std::vector<int>>(_hsm->result, solution);
}


void* HordeProcessAdapter::createSharedMemoryBlock(std::string shmemSubId, size_t size, void* data) {
    std::string id = _shmem_id + "." + shmemSubId;
    void* shmem = SharedMemory::create(id, size);
    if (data == nullptr) {
        memset(shmem, 0, size);
    } else {
        memcpy(shmem, data, size);
    }
    _shmem.insert(ShmemObject{id, shmem, size});
    return shmem;
}

