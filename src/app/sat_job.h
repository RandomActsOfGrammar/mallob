
#ifndef DOMPASCH_JOB_IMAGE_H
#define DOMPASCH_JOB_IMAGE_H

#include <string>
#include <memory>
#include <thread>
#include <assert.h>

#include "HordeLib.h"

#include "data/job.h"
#include "util/params.h"
#include "util/permutation.h"
#include "data/job_description.h"
#include "data/job_transfer.h"
#include "data/epoch_counter.h"

const int RESULT_UNKNOWN = 0;
const int RESULT_SAT = 10;
const int RESULT_UNSAT = 20;

class SatJob : public Job {

private:
    volatile bool _abort_after_initialization = false;

    std::unique_ptr<HordeLib> _solver;
    void* _clause_comm = NULL; // SatClauseCommunicator instance (avoiding fwd decl.)

    volatile bool _done_locally;

    std::thread _bg_thread;
    mutable Mutex _horde_manipulation_lock;

    float _time_of_start_solving = 0;
    float _time_of_last_comm = 0;
    float _job_comm_period;

public:

    SatJob(Parameters& params, int commSize, int worldRank, int jobId, EpochCounter& epochCounter);
    ~SatJob() override;

    bool appl_initialize() override;
    bool appl_doneInitializing() override;
    void appl_updateRole() override;
    void appl_updateDescription(int fromRevision) override;
    void appl_pause() override;
    void appl_unpause() override;
    void appl_interrupt() override;
    void appl_withdraw() override;
    int appl_solveLoop() override;

    bool appl_wantsToBeginCommunication() const override;
    void appl_beginCommunication() override;
    void appl_communicate(int source, JobMessage& msg) override;

    void appl_dumpStats() override;
    bool appl_isDestructible() override;

    // Methods that are not overridden, but use the default implementation:
    // int getDemand(int prevVolume) const override;
    // bool wantsToCommunicate() const override;

    std::unique_ptr<HordeLib>& getSolver() {
        assert(_solver != NULL);
        return _solver;
    }

    void lockHordeManipulation();
    void unlockHordeManipulation();

private:
    void extractResult(int resultCode);

    void appl_interrupt_unsafe();

    void cleanUpThread();
    void cleanUp();

    bool solverNotNull() {
        return _solver != NULL;
    }
};





#endif
