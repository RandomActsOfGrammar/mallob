/*
 * Cadical.cpp
 *
 *  Created on: Jun 26, 2020
 *      Author: schick
 */

#include <ctype.h>
#include <stdarg.h>
#include <chrono>
#include <filesystem>

#include "cadical.hpp"


Cadical::Cadical(const SolverSetup& setup)
	: PortfolioSolverInterface(setup),
	  solver(new CaDiCaL::Solver), terminator(*setup.logger), 
	  learner(_setup), learnSource(_setup, [this]() {
		  Mallob::Clause c;
		  fetchLearnedClause(c, AdaptiveClauseDatabase::ANY);
		  return c;
	  }) {
	// MWW HACK.
	// solver->set("log", 1);
	solver->connect_terminator(&terminator);
	solver->connect_learn_source(&learnSource);
        solver->set("binary", false);
        std::string logdir = setup.proofDir;
        //length of the directory + space for filename
        char *filename_alone = new char [20];
        sprintf(filename_alone, "proof.%d.frat", setup.globalId + 1);
        //hanlde the joining string already being at the end
        std::filesystem::path dir(logdir);
        std::filesystem::path file(filename_alone);
        std::filesystem::path full_path = dir / file;
        solver->trace_proof(full_path.string().c_str());
}

void Cadical::addLiteral(int lit) {
	solver->add(lit);
}

void Cadical::diversify(int seed) {

	if (seedSet) return;

	// Options may only be set in the initialization phase, so the seed cannot be re-set
	bool okay = solver->set("seed", seed);
	assert(okay);

	// In certified UNSAT mode?
	if (getSolverSetup().certifiedUnsat) {

		int solverRank = getSolverSetup().globalId;
		int maxNumSolvers = getSolverSetup().maxNumSolvers;
		LOGGER(_logger, V3_VERB, "Diversifying rank=%i size=%i DI=%i with certified UNSAT support\n", 
			solverRank, maxNumSolvers, getDiversificationIndex());
                //Need to do +1 so we don't start at 0
		okay = solver->set("instance_num", solverRank + 1); assert(okay);
		okay = solver->set("total_instances", maxNumSolvers); assert(okay);
                okay = solver->set("num_original_clauses", getSolverSetup().numOriginalClauses); assert(okay);

		// Check that a version of CaDiCaL is used which has all the unsupported options switched off
		auto requiredOptionsZero = {"elim", "decompose", "ternary", "vivify", "probe", "transred"};
		for (auto& option : requiredOptionsZero) {
			assert(solver->get(option) == 0 
				|| log_return_false("CaDiCaL is configured with option \"%s\" "
				"which is unsupported for certified UNSAT!\n", option));
		}
		
		// Only use shuffling as native diversification
		if (getDiversificationIndex() > 0) {
			okay = solver->set("shuffle", 1); assert(okay);
			okay = solver->set("shufflerandom", 1); assert(okay);
			okay = solver->set("shufflequeue", 1); assert(okay);
			okay = solver->set("shufflescores", 1); assert(okay);
		}
		return;
	}

	// Normal mode of execution
	LOGGER(_logger, V3_VERB, "Diversifying %i\n", getDiversificationIndex());

	switch (getDiversificationIndex() % getNumOriginalDiversifications()) {
	/*
	// original diversification
	case 0: okay = solver->configure("default"); break;
	case 1: okay = solver->configure("plain"); break;
	case 2: okay = solver->configure("sat"); break;
	case 3: okay = solver->configure("unsat"); break;
	case 4: okay = solver->set("chronoalways", 1); break;
	case 5: okay = solver->set("condition", 1); break;
	case 6: okay = solver->set("cover", 1); break;
	case 7: okay = solver->set("restartint", 100); break;
	case 8: okay = solver->set("shuffle", 1) && solver->set("shufflerandom", 1); break;
	case 9: okay = solver->set("walk", 0); break;
	case 10: okay = solver->set("inprocessing", 0); break;
	case 11: okay = solver->set("phase", 0); break;
	case 12: okay = solver->set("decompose", 0); break;
	case 13: okay = solver->set("elim", 0); break;
	case 14: okay = solver->set("minimize", 0); break;
	*/
	// Greedy 10-portfolio according to tests of the above configurations on SAT2020 instances
	case 0: okay = solver->set("phase", 0); break;
	case 1: okay = solver->configure("sat"); break;
	case 2: okay = solver->set("elim", 0); break;
	case 3: okay = solver->configure("unsat"); break;
	case 4: okay = solver->set("condition", 1); break;
	case 5: okay = solver->set("walk", 0); break;
	case 6: okay = solver->set("restartint", 100); break;
	case 7: okay = solver->set("cover", 1); break;
	case 8: okay = solver->set("shuffle", 1) && solver->set("shufflerandom", 1); break;
	case 9: okay = solver->set("inprocessing", 0); break;
	}
	assert(okay);
	seedSet = true;
	setClauseSharing(getNumOriginalDiversifications());
}

int Cadical::getNumOriginalDiversifications() {
	if (getSolverSetup().certifiedUnsat) return 1;
	return 15;
}

void Cadical::setPhase(const int var, const bool phase) {
	solver->phase(phase ? var : -var);
}

// Solve the formula with a given set of assumptions
// return 10 for SAT, 20 for UNSAT, 0 for UNKNOWN
SatResult Cadical::solve(size_t numAssumptions, const int* assumptions) {

	// add the learned clauses
	learnMutex.lock();
	for (auto clauseToAdd : learnedClauses) {
		for (auto litToAdd : clauseToAdd) {
			addLiteral(litToAdd);
		}
		addLiteral(0);
	}
	learnedClauses.clear();
	learnMutex.unlock();

	// set the assumptions
	this->assumptions.clear();
	for (size_t i = 0; i < numAssumptions; i++) {
		int lit = assumptions[i];
		solver->assume(lit);
		this->assumptions.push_back(lit);
	}

	// start solving
	int res = solver->solve();
	switch (res) {
	case 0:
		return UNKNOWN;
	case 10:
		return SAT;
	case 20:
		return UNSAT;
	default:
		return UNKNOWN;
	}
}

void Cadical::setSolverInterrupt() {
	terminator.setInterrupt();
}

void Cadical::unsetSolverInterrupt() {
	terminator.unsetInterrupt();
}

void Cadical::setSolverSuspend() {
    terminator.setSuspend();
}

void Cadical::unsetSolverSuspend() {
    terminator.unsetSuspend();
}


std::vector<int> Cadical::getSolution() {
	std::vector<int> result = {0};

	for (int i = 1; i <= getVariablesCount(); i++)
		result.push_back(solver->val(i));

	return result;
}

std::set<int> Cadical::getFailedAssumptions() {
	std::set<int> result;
	for (auto assumption : assumptions)
		if (solver->failed(assumption))
			result.insert(assumption);

	return result;
} 

void Cadical::setLearnedClauseCallback(const LearnedClauseCallback& callback) {
	learner.setCallback(callback);
	solver->connect_learner(&learner);
}

int Cadical::getVariablesCount() {
	return solver->vars();
}

int Cadical::getSplittingVariable() {
	return solver->lookahead();
}

void Cadical::writeStatistics(SolverStatistics& stats) {
	CaDiCaL::Solver::Statistics s = solver->get_stats();
	stats.conflicts = s.conflicts;
	stats.decisions = s.decisions;
	stats.propagations = s.propagations;
	stats.restarts = s.restarts;
}

Cadical::~Cadical() {
	solver.release();
}
