
#pragma once

#include "cadical_interface.hpp"
#include "portfolio_solver_interface.hpp"
#include "util/sys/atomics.hpp"

// HACK! 
#define MWW_DEBUG_HACK
#ifdef MWW_DEBUG_HACK
  #include <string>
  #include <sstream>
  #include <fstream>
  #include <iostream>
  #define MWW_COND_EXECUTE(cmd) cmd
#else
  #define MWW_COND_EXECUTE(cmd)
#endif

struct MallobLearnSource : public CaDiCaL::LearnSource {

private:
    Logger& _log;
    std::function<Mallob::Clause()> _clause_fetcher;
    std::vector<int> _next_clause;
    MWW_COND_EXECUTE(
        // MWW: awful, temporary hack for debugging.
        std::ofstream clause_writer;
    )

public:
    MallobLearnSource(const SolverSetup& setup, std::function<Mallob::Clause()> clauseFetcher) : 
        _log(*setup.logger),
        _clause_fetcher(clauseFetcher) {

		MWW_COND_EXECUTE({
            //
            // MWW: Terrible, horrible hack for debugging.
            //
            std::ostringstream fp_stream;
            fp_stream << "/logs/" << setup.globalId << "__" << setup.localId << "__learn_source";
            std::string fname = fp_stream.str();
            std::cout << "Opening: " << fname << std::endl;
            clause_writer.open(fname);
            // clause_writer << setup.jobname << std::endl;
            //
            // MWW: End terrible, horrible hack.
        })

        }
    ~MallobLearnSource() { }

    bool hasNextClause() override {
        auto clause = _clause_fetcher();
        if (clause.begin == nullptr) return false;
        
        assert(clause.size >= MALLOB_CLAUSE_METADATA_SIZE+1);
        
        if (clause.size == MALLOB_CLAUSE_METADATA_SIZE+1) {
            _next_clause.resize(MALLOB_CLAUSE_METADATA_SIZE+1);
            for (size_t i = 0; i < _next_clause.size(); ++i) {
                _next_clause[i] = clause.begin[i];
            }

            if (MALLOB_CLAUSE_METADATA_SIZE == 2) {
                uint64_t clauseId;
                memcpy(&clauseId, clause.begin, sizeof(uint64_t));
                LOG(V5_DEBG, "IMPORT ID=%ld len=%i\n", clauseId, 1);
            }

            return true;
        }

        if (MALLOB_CLAUSE_METADATA_SIZE == 2) {
            uint64_t clauseId;
            memcpy(&clauseId, clause.begin, sizeof(uint64_t));
            LOG(V5_DEBG, "IMPORT ID=%ld len=%i\n", clauseId, clause.size-2);
        }

        _next_clause.resize(clause.size+1);
        // In CaDiCaL, LBD scores are represented from 1 to len-1. => Decrement LBD.
        _next_clause[0] = clause.lbd-1;
        for (size_t i = 0; i < clause.size; i++) {
            _next_clause[i+1] = clause.begin[i];
        }
        return true;
    }

    const std::vector<int>& getNextClause() override {
		MWW_COND_EXECUTE({
            //
            // MWW: Terrible, horrible hack for debugging.
            //
            std::string clauseString;
            for (size_t i = 0; i < _next_clause.size(); ++i)
                clauseString += std::to_string(_next_clause[i]) + " ";
            clause_writer << clauseString << std::endl;
            clause_writer.flush();
        })

        return _next_clause;
    }
};
