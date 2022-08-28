#!/usr/bin/env python3
import sys
import glob


def main(): 
    # read all files that have a matching postfix.
    # for each clause output file, determine that the clauses defined are unique.
    # for each clause input file, ensure that each clause is a member of one of the output files.

    emitted_set = set()

    rootdir = '/logs'
    for path in glob.glob(f'{rootdir}/*learner'):
        print(f"An expected proof learner (emitter) path is: {path}")
        with open(path, 'r') as emitted_clause_file:
            clauses = emitted_clause_file.readlines()
            for clause in clauses:
                elems = clause.split()
                elems.sort()
                ordered = ' '.join(elems)
                if ordered in emitted_set:
                    raise Exception(f"Clause: {clause} has been added twice to the set of clauses.")
                emitted_set.add(ordered)

    print(f"Now checking clauses are subsets")
    for path in glob.glob(f'{rootdir}/*learn_source'):
        print(f"An expected proof learn source (ingestor) path is: {path}")
        with open(path, 'r') as ingested_clause_file:
            clauses = ingested_clause_file.readlines()
            for clause in clauses:
                elems = clause.split()
                elems.sort()
                ordered = ' '.join(elems)
                if not ordered in emitted_set:
                    raise Exception(f"Clause: {clause} was read but never added to the set of clauses")

if __name__ == "__main__":
    main()