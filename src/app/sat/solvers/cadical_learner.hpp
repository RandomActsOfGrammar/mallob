
#pragma once

#include "util/assert.hpp"

#include "cadical_interface.hpp"
#include "portfolio_solver_interface.hpp"
using namespace Mallob;

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

struct HordeLearner : public CaDiCaL::Learner {

private:
	const SolverSetup& _setup;
	LearnedClauseCallback _callback;

	// These should be adjacent for locality.
	Clause _current_clause;
	std::vector<int> _current_lits;
	
	int _glue_limit;
	unsigned long _num_produced;

    MWW_COND_EXECUTE(
		// MWW: awful, temporary hack for debugging.
		std::ofstream clause_writer;
	)
	const int GLUE_SIZE = 1;
	const int CLAUSE_ADDITIONS = GLUE_SIZE + MALLOB_CLAUSE_METADATA_SIZE; 

public:
	HordeLearner(const SolverSetup& setup) : _setup(setup), 
			_current_lits(CLAUSE_ADDITIONS+setup.strictClauseLengthLimit, 0), 
			_glue_limit(_setup.strictLbdLimit) {
		
		_current_clause.begin = _current_lits.data()+1;
		_current_clause.size = 0;

		MWW_COND_EXECUTE({
			//
			// MWW: Terrible, horrible hack for debugging.
			//
			std::ostringstream fp_stream;
			fp_stream << "/logs/" << setup.globalId << "__" << setup.localId << "__learner";
			std::string fname = fp_stream.str();
			std::cout << "Opening: " << fname << std::endl;
			clause_writer.open(fname);
			//clause_writer << setup.jobname << std::endl;
			//
			// MWW: End terrible, horrible hack.
		})
	}
	~HordeLearner() override { }

  	bool learning(int size) override {
		return size > 0 && size <= _setup.strictClauseLengthLimit;
	}

	inline void learn(int lit) override {
		//LOG(V5_DEBG, "LEARN %i\n", lit);
		if (_current_clause.size < CLAUSE_ADDITIONS || lit != 0) {
			// Received a literal.  Vector is pre-allocated.
			if (_current_clause.size >= CLAUSE_ADDITIONS+_setup.strictClauseLengthLimit) {
				printf("Current clause size: %d is larger than length limit %d", 
					_current_clause.size, _setup.strictClauseLengthLimit);
				assert(_current_clause.size < 1+_setup.strictClauseLengthLimit);
			}
			_current_lits[_current_clause.size++] = lit;
			return;
		} 

		// Received a zero - clause is finished
		_num_produced++;

		MWW_COND_EXECUTE({
			//
			// MWW: Terrible, horrible hack for debugging.
			//
			std::string clauseString;
			for (size_t i = 0; i < _current_clause.size; ++i)
				clauseString += std::to_string(_current_lits[i]) + " ";
			clause_writer << clauseString << std::endl;
			clause_writer.flush();
			//
			// MWW: End terrible, horrible hack.
		})

		bool eligible = true;
		assert(MALLOB_CLAUSE_METADATA_SIZE == 2 || MALLOB_CLAUSE_METADATA_SIZE == 0);

		// Non-unit clause.
		if (_current_clause.size > MALLOB_CLAUSE_METADATA_SIZE+1) {
			assert(_current_clause.size >= MALLOB_CLAUSE_METADATA_SIZE+3); // glue value plus at least two literals
			// subtract LBD value which was added to the clause length as well
			_current_clause.size--; 
			// Non-unit clause: First integer is glue value.
			// In CaDiCaL, LBD scores are represented from 1 to len-1. => Increment LBD.
			_current_clause.lbd = _current_lits[0]+1;
			if (_current_clause.lbd > _glue_limit) eligible = false;

			if (MALLOB_CLAUSE_METADATA_SIZE == 2) {
				uint64_t clauseId;
				memcpy(&clauseId, _current_lits.data()+1, sizeof(uint64_t));
				LOG(V5_DEBG, "EXPORT ID=%ld len=%i\n", clauseId, _current_clause.size-2);
			}
		} else {
			// Unit clause
			if (MALLOB_CLAUSE_METADATA_SIZE == 2) {
				uint64_t clauseId;
				memcpy(&clauseId, _current_lits.data(), sizeof(uint64_t));
				LOG(V5_DEBG, "EXPORT ID=%ld len=%i\n", clauseId, 1);
			}
			
			_current_clause.lbd = 1;
			// copy first k+1 literals at positions 0..k to positions 1..k+1
			for (size_t i = MALLOB_CLAUSE_METADATA_SIZE+1; i > 0; i--) {
				_current_lits[i] = _current_lits[i-1];
			}
		}
		
		// Relies on _current_clause.begin being set to 1+ array.data().
		// Export clause (if eligible), reset current clause
		if (eligible) _callback(_current_clause, _setup.localId);
		_current_clause.size = 0;
	}

    void setCallback(const LearnedClauseCallback& callback) {
        _callback = callback;
    }

	unsigned long getNumProduced() const {
		return _num_produced;
	}
};