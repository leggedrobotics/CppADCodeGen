#ifndef CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_HESS_INCLUDED
#define CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_HESS_INCLUDED
/* --------------------------------------------------------------------------
 *  CppADCodeGen: C++ Algorithmic Differentiation with Source Code Generation:
 *    Copyright (C) 2013 Ciengis
 *
 *  CppADCodeGen is distributed under multiple licenses:
 *
 *   - Eclipse Public License Version 1.0 (EPL1), and
 *   - GNU General Public License Version 3 (GPL3).
 *
 *  EPL1 terms and conditions can be found in the file "epl-v10.txt", while
 *  terms and conditions for the GPL3 can be found in the file "gpl3.txt".
 * ----------------------------------------------------------------------------
 * Author: Joao Leal
 */

namespace CppAD {

    namespace loops {

        class HessianElement {
        public:
            size_t location; // location in the compressed hessian vector
            size_t row;
            unsigned short count; // number of times to be added to that location

            inline HessianElement() :
                location(std::numeric_limits<size_t>::max()),
                row(std::numeric_limits<size_t>::max()),
                count(0) {
            }

        };

        template<class Base>
        std::pair<CG<Base>, IndexPattern*> createHessianContribution(CodeHandler<Base>& handler,
                                                                     const std::vector<HessianElement>& positions,
                                                                     const CG<Base>& ddfdxdx,
                                                                     IndexOperationNode<Base>& iterationIndexOp,
                                                                     vector<IfElseInfo<Base> >& ifElses);

    }

    /***************************************************************************
     *  Methods related with loop insertion into the operation graph
     **************************************************************************/

    template<class Base>
    void CLangCompileModelHelper<Base>::analyseSparseHessianWithLoops(const std::vector<size_t>& lowerHessRows,
                                                                      const std::vector<size_t>& lowerHessCols,
                                                                      const std::vector<size_t>& lowerHessOrder,
                                                                      vector<std::set<size_t> >& noLoopEvalJacSparsity,
                                                                      vector<std::set<size_t> >& noLoopEvalHessSparsity,
                                                                      vector<std::map<size_t, std::set<size_t> > >& noLoopEvalHessLocations,
                                                                      std::map<LoopModel<Base>*, loops::HessianWithLoopsInfo<Base> >& loopHessInfo,
                                                                      bool useSymmetry) {
        using namespace std;
        using namespace CppAD::loops;
        using CppAD::vector;

        size_t nonIndexdedEqSize = _funNoLoops != NULL ? _funNoLoops->getOrigDependentIndexes().size() : 0;

        /**
         * determine sparsities
         */
        typename std::set<LoopModel<Base>*>::const_iterator itloop;
        for (itloop = _loopTapes.begin(); itloop != _loopTapes.end(); ++itloop) {
            LoopModel<Base>* l = *itloop;
            l->evalJacobianSparsity();
            l->evalHessianSparsity();
        }

        if (_funNoLoops != NULL) {
            _funNoLoops->evalJacobianSparsity();
            _funNoLoops->evalHessianSparsity();
        }

        size_t m = _fun.Range();
        size_t n = _fun.Domain();

        size_t nnz = lowerHessRows.size();

        noLoopEvalJacSparsity.resize(_funNoLoops != NULL ? m : 0);
        noLoopEvalHessSparsity.resize(_funNoLoops != NULL ? n : 0);
        noLoopEvalHessLocations.resize(noLoopEvalHessSparsity.size());

        loopHessInfo.clear();
        for (itloop = _loopTapes.begin(); itloop != _loopTapes.end(); ++itloop) {
            LoopModel<Base>* loop = *itloop;
            HessianWithLoopsInfo<Base>& loopHessInfol = loopHessInfo[loop];
            loopHessInfol = HessianWithLoopsInfo<Base>(*loop);

            // initialize Hessian information structure
            loopHessInfol.noLoopEvalHessTempsSparsity.resize(_funNoLoops != NULL ? n : 0);
        }

        /** 
         * Load locations in the compressed Hessian
         * d      d y_i
         * d x_j2 d x_j1
         */
        for (size_t eh = 0; eh < nnz; eh++) {
            size_t j1 = lowerHessRows[eh];
            size_t j2 = lowerHessCols[eh];
            size_t e = lowerHessOrder[eh];

            if (_funNoLoops != NULL) {
                // considers only the pattern for the original equations and leaves out the temporaries
                const vector<std::set<size_t> >& dydxx = _funNoLoops->getHessianOrigEqsSparsity();
                if (dydxx.size() > 0) {
                    if (dydxx[j1].find(j2) != dydxx[j1].end()) {
                        /**
                         * Present in the equations outside the loops
                         */
                        noLoopEvalHessSparsity[j1].insert(j2);
                        noLoopEvalHessLocations[j1][j2].insert(e);
                    }
                }
            }

            for (itloop = _loopTapes.begin(); itloop != _loopTapes.end(); ++itloop) {
                LoopModel<Base>* loop = *itloop;
                size_t nIter = loop->getIterationCount();

                const std::vector<IterEquationGroup<Base> >& eqGroups = loop->getEquationsGroups();
                const vector<set<size_t> >& loopJac = loop->getJacobianSparsity();
                HessianWithLoopsInfo<Base>& loopInfo = loopHessInfo.at(loop);

                const std::vector<std::vector<LoopPosition> >& indexedIndepIndexes = loop->getIndexedIndepIndexes();
                const std::vector<LoopPosition>& nonIndexedIndepIndexes = loop->getNonIndexedIndepIndexes();
                const std::vector<LoopPosition>& temporaryIndependents = loop->getTemporaryIndependents();

                size_t nIndexed = indexedIndepIndexes.size();
                size_t nNonIndexed = nonIndexedIndepIndexes.size();

                const LoopPosition* posJ1 = loop->getNonIndexedIndepIndexes(j1);
                const LoopPosition* posJ2 = (j1 == j2) ? posJ1 : loop->getNonIndexedIndepIndexes(j2);

                size_t nEqGroups = loopInfo.equationGroups.size();
                typename set<const IterEquationGroup<Base>*>::const_iterator itg;


                for (size_t g = 0; g < nEqGroups; g++) {
                    const IterEquationGroup<Base>& group = eqGroups[g];
                    const vector<set<size_t> >& groupHess = group.getHessianSparsity();

                    /**
                     * indexed - indexed
                     * d      d f_i
                     * d x_l2 d x_l1
                     */
                    const std::vector<set<pairss> >& iter2tapeJJ = group.getHessianIndexedIndexedTapeIndexes(j1, j2);
                    for (size_t iteration = 0; iteration < iter2tapeJJ.size(); iteration++) {
                        const set<pairss>& tapePairs = iter2tapeJJ[iteration];

                        set<pairss> ::const_iterator itPairs;
                        for (itPairs = tapePairs.begin(); itPairs != tapePairs.end(); ++itPairs) {
                            size_t tape1 = itPairs->first;
                            size_t tape2 = itPairs->second;
                            pairss tape;
                            if (useSymmetry && tape1 > tape2 && groupHess[tape2].find(tape1) != groupHess[tape2].end()) {
                                tape = pairss(tape2, tape1); // work the symmetry
                            } else {
                                tape = *itPairs;
                            }

                            std::vector<HessianElement>& positions = loopInfo.equationGroups[g].indexedIndexedPositions[tape];
                            positions.resize(nIter);

                            positions[iteration].location = e;
                            positions[iteration].row = j1;
                            positions[iteration].count++;

                            loopInfo.equationGroups[g].evalHessSparsity[tape.first].insert(tape.second);
                        }
                    }


                    /**
                     * indexed - non-indexed 
                     * d      d f_i    ->   d      d f_i
                     * d x_j2 d x_l1        d x_l2 d x_j1
                     */
                    if (posJ2 != NULL) {

                        const std::vector<set<size_t> >& iter2tapeJ1OrigJ2 = group.getHessianIndexedNonIndexedTapeIndexes(j1, j2);
                        for (size_t iteration = 0; iteration < iter2tapeJ1OrigJ2.size(); iteration++) {
                            const set<size_t>& tapeJ1s = iter2tapeJ1OrigJ2[iteration];

                            set<size_t>::const_iterator ittj1;
                            for (ittj1 = tapeJ1s.begin(); ittj1 != tapeJ1s.end(); ++ittj1) {
                                size_t tapeJ1 = *ittj1;

                                bool flip = useSymmetry && groupHess[posJ2->tape].find(tapeJ1) != groupHess[posJ2->tape].end();

                                std::vector<HessianElement>* positions;
                                if (flip) {
                                    positions = &loopInfo.equationGroups[g].nonIndexedIndexedPositions[pairss(posJ2->tape, tapeJ1)];
                                    loopInfo.equationGroups[g].evalHessSparsity[posJ2->tape].insert(tapeJ1);
                                } else {
                                    positions = &loopInfo.equationGroups[g].indexedNonIndexedPositions[pairss(tapeJ1, posJ2->tape)];
                                    loopInfo.equationGroups[g].evalHessSparsity[tapeJ1].insert(posJ2->tape);
                                }

                                positions->resize(nIter);
                                (*positions)[iteration].location = e;
                                (*positions)[iteration].row = j1;
                                (*positions)[iteration].count++;
                            }
                        }

                    }
                }

                /**
                 * indexed - constant z
                 * d     d f_i    .   d z_k
                 * d z_k d x_l1       d x_j2
                 */
                if (_funNoLoops != NULL) {
                    map<size_t, set<size_t> > iter2tapeJ1 = loop->getIndexedTapeIndexes(j1);
                    map<size_t, set<size_t> >::const_iterator itIter;
                    for (itIter = iter2tapeJ1.begin(); itIter != iter2tapeJ1.end(); ++itIter) {
                        size_t iteration = itIter->first;
                        const set<size_t>& tapeJ1s = itIter->second;
                        const set<const IterEquationGroup<Base>*>& groups = loop->getIterationEquationsGroup()[iteration];

                        set<size_t>::const_iterator itTapeJ1;
                        for (itTapeJ1 = tapeJ1s.begin(); itTapeJ1 != tapeJ1s.end(); ++itTapeJ1) {
                            size_t tapeJ1 = *itTapeJ1;

                            typename set<const IterEquationGroup<Base>*>::const_iterator itg;
                            for (itg = groups.begin(); itg != groups.end(); ++itg) {
                                const IterEquationGroup<Base>& group = **itg;
                                size_t g = group.index;
                                HessianWithLoopsEquationGroupInfo<Base>& groupInfo = loopInfo.equationGroups[g];
                                const vector<set<size_t> >& groupHess = group.getHessianSparsity();

                                set<size_t>::const_iterator itz = groupHess[tapeJ1].lower_bound(nIndexed + nNonIndexed);

                                pairss pos(tapeJ1, j2);
                                bool used = false;

                                // loop temporary variables
                                for (; itz != groupHess[tapeJ1].end(); ++itz) {
                                    size_t tapeJ = *itz;
                                    size_t k = temporaryIndependents[tapeJ - nIndexed - nNonIndexed].original;

                                    /**
                                     * check if this temporary depends on j2
                                     */
                                    const set<size_t>& sparsity = _funNoLoops->getJacobianSparsity()[nonIndexdedEqSize + k];
                                    if (sparsity.find(j2) != sparsity.end()) {
                                        noLoopEvalJacSparsity[nonIndexdedEqSize + k].insert(j2); // element required

                                        size_t tapeK = loop->getTempIndepIndexes(k)->tape;

                                        used = true;

                                        set<size_t>& evals = groupInfo.indexedTempEvals[pos];
                                        evals.insert(k);

                                        groupInfo.evalHessSparsity[tapeJ1].insert(tapeK);
                                    }
                                }


                                if (used) {
                                    std::vector<HessianElement>& positions = groupInfo.indexedTempPositions[pos];
                                    positions.resize(nIter);

                                    positions[iteration].location = e;
                                    positions[iteration].row = j1;
                                    positions[iteration].count++;
                                }

                            }

                        }

                    }
                }

                /**
                 * non-indexed - indexed
                 * d      d f_i
                 * d x_l2 d x_j1
                 */
                if (posJ1 != NULL) {

                    for (size_t g = 0; g < nEqGroups; g++) {
                        const IterEquationGroup<Base>& group = eqGroups[g];

                        const std::vector<set<size_t> >& iter2TapeJ2 = group.getHessianNonIndexedIndexedTapeIndexes(j1, j2);
                        for (size_t iteration = 0; iteration < iter2TapeJ2.size(); iteration++) {
                            const set<size_t>& tapeJ2s = iter2TapeJ2[iteration];

                            set<size_t>::const_iterator ittj2;
                            for (ittj2 = tapeJ2s.begin(); ittj2 != tapeJ2s.end(); ++ittj2) {
                                size_t tapeJ2 = *ittj2;

                                std::vector<HessianElement>& positions = loopInfo.equationGroups[g].nonIndexedIndexedPositions[pairss(posJ1->tape, tapeJ2)];
                                positions.resize(nIter);
                                positions[iteration].location = e;
                                positions[iteration].row = j1;
                                positions[iteration].count++;

                                loopInfo.equationGroups[g].evalHessSparsity[posJ1->tape].insert(tapeJ2);
                            }
                        }
                    }
                }

                /**
                 * non-indexed - non-indexed
                 * d      d f_i
                 * d x_j2 d x_j1
                 */
                bool jInNonIndexed = false;
                pairss orig(j1, j2);

                if (posJ1 != NULL && posJ2 != NULL) {
                    for (size_t g = 0; g < nEqGroups; g++) {
                        const IterEquationGroup<Base>& group = eqGroups[g];

                        const set<pairss>& orig1orig2 = group.getHessianNonIndexedNonIndexedIndexes();
                        if (orig1orig2.find(orig) != orig1orig2.end()) {
                            jInNonIndexed = true;

                            loopInfo.equationGroups[g].nonIndexedNonIndexedEvals.insert(orig);
                            loopInfo.equationGroups[g].evalHessSparsity[posJ1->tape].insert(posJ2->tape);
                        }

                    }

                    if (jInNonIndexed)
                        loopInfo.nonIndexedNonIndexedPosition[orig] = e;
                }

                /**
                 * non-indexed - temporaries
                 * d     d f_i   .  d z_k
                 * d z_k d x_j1     d x_j2
                 */
                if (_funNoLoops != NULL && posJ1 != NULL) {

                    for (size_t g = 0; g < nEqGroups; g++) {
                        const IterEquationGroup<Base>& group = eqGroups[g];

                        const set<size_t>& hessRow = group.getHessianSparsity()[posJ1->tape];
                        set<size_t>::const_iterator itz = hessRow.lower_bound(nIndexed + nNonIndexed);

                        // loop temporary variables
                        for (; itz != hessRow.end(); ++itz) {
                            size_t tapeJ = *itz;
                            size_t k = temporaryIndependents[tapeJ - nIndexed - nNonIndexed].original;

                            // Jacobian of g for k must have j2
                            const set<size_t>& gJacRow = _funNoLoops->getJacobianSparsity()[nonIndexdedEqSize + k];
                            if (gJacRow.find(j2) != gJacRow.end()) {
                                noLoopEvalJacSparsity[nonIndexdedEqSize + k].insert(j2); // element required

                                if (!jInNonIndexed) {
                                    jInNonIndexed = true;
                                    CPPADCG_ASSERT_KNOWN(loopInfo.nonIndexedNonIndexedPosition.find(orig) == loopInfo.nonIndexedNonIndexedPosition.end(),
                                                         "Repeated hessian elements requested");
                                    loopInfo.nonIndexedNonIndexedPosition[orig] = e;
                                }

                                size_t tapeK = loop->getTempIndepIndexes(k)->tape;
                                loopInfo.equationGroups[g].nonIndexedTempEvals[orig].insert(k);
                                loopInfo.equationGroups[g].evalHessSparsity[posJ1->tape].insert(tapeK);
                            }

                        }
                    }
                }

                /**
                 * temporaries
                 */
                if (_funNoLoops != NULL) {
                    const vector<set<size_t> >& gJac = _funNoLoops->getJacobianSparsity();
                    size_t nk = _funNoLoops->getTemporaryDependentCount();
                    size_t nOrigEq = _funNoLoops->getTapeDependentCount() - nk;

                    const vector<set<size_t> >& dzdxx = _funNoLoops->getHessianTempEqsSparsity();

                    std::vector<std::set<size_t> > usedTapeJ2(nEqGroups);

                    for (size_t k1 = 0; k1 < nk; k1++) {
                        if (gJac[nOrigEq + k1].find(j1) == gJac[nOrigEq + k1].end()) {
                            continue;
                        }

                        const LoopPosition* posK1 = loop->getTempIndepIndexes(k1);
                        if (posK1 == NULL) {
                            continue;
                        }

                        /**
                         * temporary - indexed
                         * d     d f_i
                         * d x_l d z_k1
                         */

                        for (size_t g = 0; g < nEqGroups; g++) {
                            const IterEquationGroup<Base>& group = eqGroups[g];
                            const vector<set<size_t> >& groupHess = group.getHessianSparsity();
                            HessianWithLoopsEquationGroupInfo<Base>& groupHessInfo = loopInfo.equationGroups[g];

                            const map<size_t, set<size_t> >& tapeJ22Iter = group.getHessianTempIndexedTapeIndexes(k1, j2);
                            map<size_t, set<size_t> >::const_iterator ittj22iter;
                            for (ittj22iter = tapeJ22Iter.begin(); ittj22iter != tapeJ22Iter.end(); ++ittj22iter) {
                                size_t tapeJ2 = ittj22iter->first;
                                const set<size_t>& iterations = ittj22iter->second;

                                bool used = usedTapeJ2[g].find(tapeJ2) != usedTapeJ2[g].end();
                                bool added = false;

                                set<size_t>::const_iterator itIt;
                                for (itIt = iterations.begin(); itIt != iterations.end(); ++itIt) {
                                    size_t iteration = *itIt;

                                    std::vector<HessianElement>* positions = NULL;

                                    bool flip = useSymmetry && groupHess[tapeJ2].find(posK1->tape) != groupHess[tapeJ2].end();
                                    if (flip) {
                                        if (!used) {
                                            pairss pos(tapeJ2, j1);
                                            positions = &groupHessInfo.indexedTempPositions[pos];
                                        }
                                        groupHessInfo.evalHessSparsity[tapeJ2].insert(posK1->tape);
                                    } else {
                                        if (!used) {
                                            pairss pos(j1, tapeJ2);
                                            positions = &groupHessInfo.tempIndexedPositions[pos];
                                        }
                                        groupHessInfo.evalHessSparsity[posK1->tape].insert(tapeJ2);
                                    }

                                    if (positions != NULL) {
                                        positions->resize(nIter);

                                        (*positions)[iteration].location = e;
                                        (*positions)[iteration].row = j1;
                                        (*positions)[iteration].count++;
                                        usedTapeJ2[g].insert(tapeJ2);
                                    }

                                    if (!added) {
                                        added = true;
                                        set<size_t>& evals = groupHessInfo.indexedTempEvals[pairss(tapeJ2, j1)];
                                        evals.insert(k1);
                                    }
                                }

                            }

                            /**
                             * temporary - non-indexed
                             * d      d f_i
                             * d x_j2 d z_k1
                             */
                            if (posJ2 != NULL) {
                                const set<size_t>& hessRow = group.getHessianSparsity()[posK1->tape];

                                if (hessRow.find(j2) != hessRow.end()) {
                                    if (!jInNonIndexed) {
                                        jInNonIndexed = true;
                                        CPPADCG_ASSERT_KNOWN(loopInfo.nonIndexedNonIndexedPosition.find(orig) == loopInfo.nonIndexedNonIndexedPosition.end(),
                                                             "Repeated hessian elements requested");
                                        loopInfo.nonIndexedNonIndexedPosition[orig] = e;
                                    }

                                    groupHessInfo.tempNonIndexedEvals[orig].insert(k1);
                                    groupHessInfo.evalHessSparsity[posK1->tape].insert(posJ2->tape);
                                }
                            }


                            /**
                             * temporary - temporary
                             *    d  d f_i    .  d z_k2
                             * d z_k2 d z_k1     d x_j2
                             */
                            // loop Hessian row
                            const set<size_t>& hessRow = group.getHessianSparsity()[posK1->tape];
                            set<size_t>::const_iterator itTapeJ2 = hessRow.lower_bound(nIndexed + nNonIndexed);
                            for (; itTapeJ2 != hessRow.end(); ++itTapeJ2) {
                                size_t tapeK2 = *itTapeJ2;
                                size_t k2 = loop->getTemporaryIndependents()[tapeK2 - nIndexed - nNonIndexed].original;

                                const set<size_t>& jacZk2Row = gJac[nOrigEq + k2];
                                if (jacZk2Row.find(j2) != jacZk2Row.end()) { // is this check truly needed?

                                    if (!jInNonIndexed) {
                                        jInNonIndexed = true;
                                        CPPADCG_ASSERT_KNOWN(loopInfo.nonIndexedNonIndexedPosition.find(orig) == loopInfo.nonIndexedNonIndexedPosition.end(),
                                                             "Repeated hessian elements requested");
                                        loopInfo.nonIndexedNonIndexedPosition[orig] = e;
                                    }

                                    groupHessInfo.tempTempEvals[orig][k1].insert(k2);
                                    groupHessInfo.evalHessSparsity[posK1->tape].insert(tapeK2);

                                    noLoopEvalJacSparsity[nOrigEq + k2].insert(j2); /////// @todo: repeated operation for each group (only one needed)
                                }
                            }
                        }

                        //
                        noLoopEvalJacSparsity[nOrigEq + k1].insert(j1);

                        /**
                         * temporary - temporary
                         * d f_i   .  d      d z_k1
                         * d z_k1     d x_j2 d x_j1
                         */
                        if (dzdxx[j1].find(j2) != dzdxx[j1].end()) {

                            for (size_t i = 0; i < loopJac.size(); i++) {
                                const set<size_t>& fJacRow = loopJac[i];

                                if (fJacRow.find(posK1->tape) != fJacRow.end()) {
                                    if (!jInNonIndexed) {
                                        CPPADCG_ASSERT_KNOWN(loopInfo.nonIndexedNonIndexedPosition.find(orig) == loopInfo.nonIndexedNonIndexedPosition.end(),
                                                             "Repeated hessian elements requested");

                                        loopInfo.nonIndexedNonIndexedPosition[orig] = e;
                                        jInNonIndexed = true;
                                    }

                                    loopInfo.nonLoopNonIndexedNonIndexed[orig].insert(k1);
                                    loopInfo.evalJacSparsity[i].insert(posK1->tape);
                                    loopInfo.noLoopEvalHessTempsSparsity[j1].insert(j2);
                                }
                            }
                        }
                    }
                }

            }
        }
    }

    template<class Base>
    inline void addContribution(std::vector<std::pair<CppAD::CG<Base>, IndexPattern*> >& indexedLoopResults,
                                size_t& hessLE,
                                const std::pair<CppAD::CG<Base>, IndexPattern*>& val) {
        if (!val.first.isParameter() || !val.first.IdenticalZero()) {
            if (indexedLoopResults.size() == hessLE) {
                indexedLoopResults.resize(3 * hessLE / 2 + 1);
            }
            indexedLoopResults[hessLE++] = val;
        }
    }

    template<class Base>
    vector<CG<Base> > CLangCompileModelHelper<Base>::prepareSparseHessianWithLoops(CodeHandler<Base>& handler,
                                                                                   vector<CGBase>& x,
                                                                                   vector<CGBase>& w,
                                                                                   const std::vector<size_t>& lowerHessRows,
                                                                                   const std::vector<size_t>& lowerHessCols,
                                                                                   const std::vector<size_t>& lowerHessOrder,
                                                                                   const std::map<size_t, size_t>& duplicates) {
        using namespace std;
        using namespace CppAD::loops;
        using CppAD::vector;

        handler.setZeroDependents(true);

        size_t nonIndexdedEqSize = _funNoLoops != NULL ? _funNoLoops->getOrigDependentIndexes().size() : 0;

        size_t maxLoc = _hessSparsity.rows.size();
        vector<CGBase> hess(maxLoc);

        vector<set<size_t> > noLoopEvalJacSparsity;
        vector<set<size_t> > noLoopEvalHessSparsity;
        vector<map<size_t, set<size_t> > > noLoopEvalHessLocations;
        map<LoopModel<Base>*, HessianWithLoopsInfo<Base> > loopHessInfo;

        /** 
         * Load locations in the compressed Hessian
         * d      d y_i
         * d x_j2 d x_j1
         */
        analyseSparseHessianWithLoops(lowerHessRows, lowerHessCols, lowerHessOrder,
                                      noLoopEvalJacSparsity, noLoopEvalHessSparsity,
                                      noLoopEvalHessLocations, loopHessInfo, true);

        /***********************************************************************
         *        generate the operation graph
         **********************************************************************/
        /**
         * prepare loop independents
         */
        IndexDclrOperationNode<Base>* iterationIndexDcl = new IndexDclrOperationNode<Base>(LoopModel<Base>::ITERATION_INDEX_NAME);
        handler.manageOperationNodeMemory(iterationIndexDcl);

        // temporaries (zero order)
        vector<CGBase> tmpsAlias;
        if (_funNoLoops != NULL) {
            ADFun<CGBase>& fun = _funNoLoops->getTape();

            tmpsAlias.resize(fun.Range() - nonIndexdedEqSize);
            for (size_t k = 0; k < tmpsAlias.size(); k++) {
                // to be defined later
                tmpsAlias[k] = handler.createCG(new OperationNode<Base>(CGAliasOp));
            }
        }

        /**
         * prepare loop independents
         */
        typename map<LoopModel<Base>*, HessianWithLoopsInfo<Base> >::iterator itLoop2Info;
        for (itLoop2Info = loopHessInfo.begin(); itLoop2Info != loopHessInfo.end(); ++itLoop2Info) {
            LoopModel<Base>& lModel = *itLoop2Info->first;
            HessianWithLoopsInfo<Base>& info = itLoop2Info->second;

            /**
             * make the loop start
             */
            info.loopStart = new LoopStartOperationNode<Base>(*iterationIndexDcl, lModel.getIterationCount());
            handler.manageOperationNodeMemory(info.loopStart);

            info.iterationIndexOp = new IndexOperationNode<Base>(*info.loopStart);
            handler.manageOperationNodeMemory(info.iterationIndexOp);
            set<IndexOperationNode<Base>*> indexesOps;
            indexesOps.insert(info.iterationIndexOp);

            /**
             * make the loop's indexed variables
             */
            vector<CGBase> indexedIndeps = createIndexedIndependents(handler, lModel, *info.iterationIndexOp);
            info.x = createLoopIndependentVector(handler, lModel, indexedIndeps, x, tmpsAlias);

            info.w = createLoopDependentVector(handler, lModel, *info.iterationIndexOp);
        }

        /**
         * Calculate Hessians and Jacobians
         */
        /**
         * Loops - evaluate Jacobian and Hessian
         */
        bool hasAtomics = isAtomicsUsed(); // TODO: improve this by checking only the current fun

        for (itLoop2Info = loopHessInfo.begin(); itLoop2Info != loopHessInfo.end(); ++itLoop2Info) {
            LoopModel<Base>& lModel = *itLoop2Info->first;
            HessianWithLoopsInfo<Base>& info = itLoop2Info->second;

            _cache.str("");
            _cache << "model (Jacobian + Hessian, loop " << lModel.getLoopId() << ")";
            std::string jobName = _cache.str();
            _cache.str("");
            startingJob("'" + jobName + "'", JobTimer::GRAPH);

            info.evalLoopModelJacobianHessian(hasAtomics);

            finishedJob();
        }

        /**
         * No loops
         */
        // Jacobian for temporaries
        map<size_t, map<size_t, CGBase> > dzDx;

        if (_funNoLoops != NULL) {
            ADFun<CGBase>& fun = _funNoLoops->getTape();
            vector<CGBase> yNL(fun.Range());

            /**
             * Jacobian and Hessian - temporary variables
             */
            startingJob("'model (Jacobian + Hessian, temporaries)'", JobTimer::GRAPH);

            dzDx = _funNoLoops->calculateJacobianHessianUsedByLoops(handler,
                                                                    loopHessInfo, x, yNL,
                                                                    noLoopEvalJacSparsity,
                                                                    hasAtomics);

            finishedJob();

            for (size_t i = 0; i < tmpsAlias.size(); i++)
                tmpsAlias[i].getOperationNode()->getArguments().push_back(asArgument(yNL[nonIndexdedEqSize + i]));

            for (itLoop2Info = loopHessInfo.begin(); itLoop2Info != loopHessInfo.end(); ++itLoop2Info) {
                HessianWithLoopsInfo<Base>& info = itLoop2Info->second;
                // not needed anymore:
                info.dyiDzk.clear();
            }

            /**
             * Hessian - original equations
             */
            _funNoLoops->calculateHessian4OrignalEquations(x, w,
                                                           noLoopEvalHessSparsity, noLoopEvalHessLocations,
                                                           hess);
        }

        /**
         * Loops - Hessian
         */
        for (itLoop2Info = loopHessInfo.begin(); itLoop2Info != loopHessInfo.end(); ++itLoop2Info) {
            LoopModel<Base>& lModel = *itLoop2Info->first;
            size_t nIterations = lModel.getIterationCount();
            HessianWithLoopsInfo<Base>& info = itLoop2Info->second;

            // store results in indexedLoopResults
            size_t hessElSize = info.nonIndexedNonIndexedPosition.size();
            for (size_t g = 0; g < info.equationGroups.size(); g++) {
                HessianWithLoopsEquationGroupInfo<Base>& infog = info.equationGroups[g];
                hessElSize += infog.indexedIndexedPositions.size() +
                        infog.indexedTempPositions.size() +
                        infog.nonIndexedIndexedPositions.size();
            }

            if (hessElSize == 0)
                continue; // no second order information

            std::vector<pair<CGBase, IndexPattern*> > indexedLoopResults(hessElSize);
            size_t hessLE = 0;

            /**
             * loop the groups of equations present at the same iterations
             */
            for (size_t g = 0; g < info.equationGroups.size(); g++) {

                HessianWithLoopsEquationGroupInfo<Base>& infog = info.equationGroups[g];

                /*******************************************************************
                 * indexed - indexed
                 */
                map<pairss, std::vector<HessianElement> >::const_iterator it;
                for (it = infog.indexedIndexedPositions.begin(); it != infog.indexedIndexedPositions.end(); ++it) {
                    size_t tapeJ1 = it->first.first;
                    size_t tapeJ2 = it->first.second;
                    const std::vector<HessianElement>& positions = it->second;

                    addContribution(indexedLoopResults, hessLE,
                                    createHessianContribution(handler, positions, infog.hess[tapeJ1].at(tapeJ2),
                                                              *info.iterationIndexOp, info.ifElses));
                }

                /**
                 * indexed - non-indexed
                 * - usually done by  (non-indexed - indexed) by exploiting the symmetry
                 */
                for (it = infog.indexedNonIndexedPositions.begin(); it != infog.indexedNonIndexedPositions.end(); ++it) {
                    size_t tapeJ1 = it->first.first;
                    size_t tapeJ2 = it->first.second;
                    const std::vector<HessianElement>& positions = it->second;

                    addContribution(indexedLoopResults, hessLE,
                                    createHessianContribution(handler, positions, infog.hess[tapeJ1].at(tapeJ2),
                                                              *info.iterationIndexOp, info.ifElses));
                }

                /**
                 * indexed - temporary
                 */
                map<pairss, set<size_t> >::const_iterator itEval;
                if (!infog.indexedTempPositions.empty()) {
                    for (itEval = infog.indexedTempEvals.begin(); itEval != infog.indexedTempEvals.end(); ++itEval) {
                        size_t tapeJ1 = itEval->first.first;
                        size_t j2 = itEval->first.second;
                        const set<size_t>& ks = itEval->second;

                        map<pairss, std::vector<HessianElement> >::const_iterator itPos = infog.indexedTempPositions.find(itEval->first);
                        if (itPos != infog.indexedTempPositions.end()) {
                            const std::vector<HessianElement>& positions = itPos->second;

                            CGBase hessVal = Base(0);
                            set<size_t>::const_iterator itz;
                            for (itz = ks.begin(); itz != ks.end(); ++itz) {
                                size_t k = *itz;
                                size_t tapeK = lModel.getTempIndepIndexes(k)->tape;
                                hessVal += infog.hess[tapeJ1].at(tapeK) * dzDx[k][j2];
                            }

                            addContribution(indexedLoopResults, hessLE,
                                            createHessianContribution(handler, positions, hessVal,
                                                                      *info.iterationIndexOp, info.ifElses));
                        }
                    }
                }

                /*******************************************************************
                 * non-indexed - indexed
                 */
                for (it = infog.nonIndexedIndexedPositions.begin(); it != infog.nonIndexedIndexedPositions.end(); ++it) {
                    size_t tapeJ1 = it->first.first;
                    size_t tapeJ2 = it->first.second;
                    const std::vector<HessianElement>& positions = it->second;

                    addContribution(indexedLoopResults, hessLE,
                                    createHessianContribution(handler, positions, infog.hess[tapeJ1].at(tapeJ2),
                                                              *info.iterationIndexOp, info.ifElses));
                }

                /*******************************************************************
                 * temporary - indexed
                 * 
                 *      d f_i       .    d x_k1
                 * d x_l2  d z_k1        d x_j1
                 * 
                 * -> usually done by  (indexed - temporary) by exploiting the symmetry
                 */
                if (!infog.tempIndexedPositions.empty()) {
                    for (itEval = infog.indexedTempEvals.begin(); itEval != infog.indexedTempEvals.end(); ++itEval) {
                        size_t tapeJ2 = itEval->first.first;
                        size_t j1 = itEval->first.second;
                        const set<size_t>& ks = itEval->second;

                        map<pairss, std::vector<HessianElement> >::const_iterator itPos = infog.tempIndexedPositions.find(pairss(j1, tapeJ2));
                        if (itPos != infog.tempIndexedPositions.end()) {
                            const std::vector<HessianElement>& positions = itPos->second;
                            CGBase hessVal = Base(0);
                            set<size_t>::const_iterator itz;
                            for (itz = ks.begin(); itz != ks.end(); ++itz) {
                                size_t k = *itz;
                                size_t tapeK = lModel.getTempIndepIndexes(k)->tape;
                                hessVal += infog.hess[tapeK].at(tapeJ2) * dzDx[k][j1];
                            }

                            addContribution(indexedLoopResults, hessLE,
                                            createHessianContribution(handler, positions, hessVal,
                                                                      *info.iterationIndexOp, info.ifElses));
                        }
                    }
                }

            }

            /*******************************************************************
             * contributions to a constant location
             */
            map<pairss, size_t>::const_iterator orig2PosIt;
            for (orig2PosIt = info.nonIndexedNonIndexedPosition.begin(); orig2PosIt != info.nonIndexedNonIndexedPosition.end(); ++orig2PosIt) {
                const pairss& orig = orig2PosIt->first;
                size_t e = orig2PosIt->second;

                size_t j1 = orig.first;
                size_t j2 = orig.second;
                const LoopPosition* posJ1 = lModel.getNonIndexedIndepIndexes(j1);
                const LoopPosition* posJ2 = lModel.getNonIndexedIndepIndexes(j2);

                // location
                LinearIndexPattern* pattern = new LinearIndexPattern(0, 0, 0, e);
                handler.manageLoopDependentIndexPattern(pattern);

                /**
                 * non-indexed - non-indexed
                 */
                CGBase hessVal = Base(0);

                /**
                 * loop the groups of equations present at the same iterations
                 */
                for (size_t g = 0; g < info.equationGroups.size(); g++) {
                    const IterEquationGroup<Base>& group = lModel.getEquationsGroups()[g];

                    CGBase gHessVal = Base(0);
                    HessianWithLoopsEquationGroupInfo<Base>& infog = info.equationGroups[g];

                    if (infog.nonIndexedNonIndexedEvals.find(orig) != infog.nonIndexedNonIndexedEvals.end()) {
                        gHessVal = infog.hess[posJ1->tape].at(posJ2->tape);
                    }

                    /**
                     * non-indexed - temporary
                     */
                    map<pairss, set<size_t> >::const_iterator itNT = infog.nonIndexedTempEvals.find(orig);
                    if (itNT != infog.nonIndexedTempEvals.end()) {
                        const set<size_t>& ks = itNT->second;

                        set<size_t>::const_iterator itz;
                        for (itz = ks.begin(); itz != ks.end(); ++itz) {
                            size_t k = *itz;
                            size_t tapeK = lModel.getTempIndepIndexes(k)->tape;
                            gHessVal += infog.hess[posJ1->tape].at(tapeK) * dzDx[k][j2];
                        }
                    }

                    /**
                     * temporary - non-indexed 
                     * 
                     *      d f_i       .    d x_k1
                     * d x_j2  d z_k1        d x_j1
                     */
                    map<pairss, set<size_t> >::const_iterator itTN = infog.tempNonIndexedEvals.find(orig);
                    if (itTN != infog.tempNonIndexedEvals.end()) {
                        const set<size_t>& ks = itTN->second;

                        set<size_t>::const_iterator itz;
                        for (itz = ks.begin(); itz != ks.end(); ++itz) {
                            size_t k1 = *itz;
                            size_t tapeK = lModel.getTempIndepIndexes(k1)->tape;
                            gHessVal += infog.hess[tapeK].at(posJ2->tape) * dzDx[k1][j1];
                        }
                    }

                    /**
                     * temporary - temporary
                     */
                    map<pairss, map<size_t, set<size_t> > >::const_iterator itTT = infog.tempTempEvals.find(orig);
                    if (itTT != infog.tempTempEvals.end()) {
                        const map<size_t, set<size_t> >& k1k2 = itTT->second;

                        CGBase sum = Base(0);

                        map<size_t, set<size_t> >::const_iterator itzz;
                        for (itzz = k1k2.begin(); itzz != k1k2.end(); ++itzz) {
                            size_t k1 = itzz->first;
                            const set<size_t>& k2s = itzz->second;
                            size_t tapeK1 = lModel.getTempIndepIndexes(k1)->tape;

                            CGBase tmp = Base(0);
                            for (set<size_t>::const_iterator itk2 = k2s.begin(); itk2 != k2s.end(); ++itk2) {
                                size_t k2 = *itk2;
                                size_t tapeK2 = lModel.getTempIndepIndexes(k2)->tape;

                                tmp += infog.hess[tapeK1].at(tapeK2) * dzDx[k2][j2];
                            }

                            sum += tmp * dzDx[k1][j1];
                        }

                        gHessVal += sum;
                    }

                    if (group.iterations.size() != nIterations) {
                        CGBase v = createHessianContribution(handler, *pattern, group.iterations,
                                                             nIterations, gHessVal,
                                                             *info.iterationIndexOp, info.ifElses);
                        addContribution(indexedLoopResults, hessLE, make_pair(v, (IndexPattern*) NULL));
                    } else {
                        hessVal += gHessVal;
                    }

                }

                /**
                 * temporary - temporary
                 */
                map<pairss, set<size_t> >::const_iterator itTT2 = info.nonLoopNonIndexedNonIndexed.find(orig);
                if (itTT2 != info.nonLoopNonIndexedNonIndexed.end()) {
                    hessVal += info.dzDxx.at(j1).at(j2); // it is already the sum of ddz / dx_j1 dx_j2
                }

                // place the result
                addContribution(indexedLoopResults, hessLE, make_pair(hessVal, (IndexPattern*) pattern));
            }

            indexedLoopResults.resize(hessLE);

            /**
             * make the loop end
             */
            size_t assignOrAdd = 1;
            set<IndexOperationNode<Base>*> indexesOps;
            indexesOps.insert(info.iterationIndexOp);
            info.loopEnd = createLoopEnd(handler, *info.loopStart, indexedLoopResults, indexesOps, assignOrAdd);

            std::vector<size_t>::const_iterator itE;
            for (itE = lowerHessOrder.begin(); itE != lowerHessOrder.end(); ++itE) {
                // an additional alias variable is required so that each dependent variable can have its own ID
                size_t e = *itE;
                if (hess[e].isParameter() && hess[e].IdenticalZero()) {
                    hess[e] = handler.createCG(new OperationNode<Base> (CGDependentMultiAssignOp, Argument<Base>(*info.loopEnd)));

                } else if (hess[e].getOperationNode() != NULL && hess[e].getOperationNode()->getOperationType() == CGDependentMultiAssignOp) {
                    hess[e].getOperationNode()->getArguments().push_back(Argument<Base>(*info.loopEnd));

                } else {
                    hess[e] = handler.createCG(new OperationNode<Base> (CGDependentMultiAssignOp, asArgument(hess[e]), Argument<Base>(*info.loopEnd)));
                }
            }

            // not needed anymore:
            for (size_t g = 0; g < info.equationGroups.size(); g++) {
                info.equationGroups[g].hess.clear();
            }
            info.dzDxx.clear();

            /**
             * move non-indexed expressions outside loop
             */
            moveNonIndexedOutsideLoop(handler, *info.loopStart, *info.loopEnd);
        }

        /**
         * duplicates (TODO: use loops)
         */
        // make use of the symmetry of the Hessian in order to reduce operations
        std::map<size_t, size_t>::const_iterator it2;
        for (it2 = duplicates.begin(); it2 != duplicates.end(); ++it2) {
            if (hess[it2->second].isVariable())
                hess[it2->first] = handler.createCG(new OperationNode<Base> (CGAliasOp, asArgument(hess[it2->second])));
            else
                hess[it2->first] = hess[it2->second].getValue();
        }

        return hess;
    }

    namespace loops {

        template<class Base>
        std::pair<CG<Base>, IndexPattern*> createHessianContribution(CodeHandler<Base>& handler,
                                                                     const std::vector<HessianElement>& positions,
                                                                     const CG<Base>& ddfdxdx,
                                                                     IndexOperationNode<Base>& iterationIndexOp,
                                                                     vector<IfElseInfo<Base> >& ifElses) {
            using namespace std;

            if (ddfdxdx.isParameter() && ddfdxdx.IdenticalZero()) {
                return make_pair(ddfdxdx, (IndexPattern*) NULL);
            }

            // combine iterations with the same number of additions
            map<size_t, map<size_t, size_t> > locations;
            for (size_t iter = 0; iter < positions.size(); iter++) {
                size_t c = positions[iter].count;
                if (c > 0) {
                    locations[c][iter] = positions[iter].location;
                }
            }

            map<size_t, CG<Base> > results;

            // generate the index pattern for the Hessian compressed element
            map<size_t, map<size_t, size_t> >::const_iterator countIt;
            for (countIt = locations.begin(); countIt != locations.end(); ++countIt) {
                size_t count = countIt->first;

                CG<Base> val = ddfdxdx;
                for (size_t c = 1; c < count; c++)
                    val += ddfdxdx;

                results[count] = val;
            }

            if (results.size() == 1 && locations.begin()->second.size() == positions.size()) {
                // same expression present in all iterations

                // generate the index pattern for the Hessian compressed element
                IndexPattern* pattern = IndexPattern::detect(locations.begin()->second);
                handler.manageLoopDependentIndexPattern(pattern);

                return make_pair(results.begin()->second, pattern);

            } else {
                /**
                 * must create a conditional element so that this 
                 * contribution to the Hessian is only evaluated at the
                 * relevant iterations
                 */
                map<size_t, IfBranchData<Base> > branches;

                // try to find an existing if-else where these operations can be added
                map<size_t, map<size_t, size_t> >::const_iterator countIt;
                for (countIt = locations.begin(); countIt != locations.end(); ++countIt) {

                    size_t count = countIt->first;
                    IfBranchData<Base> branch(results[count], countIt->second);
                    branches[count] = branch;
                }

                CG<Base> v = createConditionalContribution(handler,
                                                           branches,
                                                           positions.size() - 1,
                                                           positions.size(),
                                                           iterationIndexOp,
                                                           ifElses);

                return make_pair(v, (IndexPattern*) NULL);
            }

        }

        /**
         * Hessian contribution to a constant location
         */
        template<class Base>
        CG<Base> createHessianContribution(CodeHandler<Base>& handler,
                                           LinearIndexPattern& pattern,
                                           const std::set<size_t>& iterations,
                                           size_t nIterations,
                                           const CG<Base>& ddfdxdx,
                                           IndexOperationNode<Base>& iterationIndexOp,
                                           vector<IfElseInfo<Base> >& ifElses) {
            using namespace std;

            if (ddfdxdx.isParameter() && ddfdxdx.IdenticalZero()) {
                return ddfdxdx;
            }

            assert(pattern.getLinearSlopeDy() == 0); // must be a constant index

            if (iterations.size() == nIterations) {
                // same expression present in all iterations
                return ddfdxdx;

            } else {

                /**
                 * must create a conditional element so that this 
                 * contribution to the Hessian is only evaluated at the
                 * relevant iterations
                 */
                return createConditionalContribution(handler, pattern,
                                                     iterations, nIterations - 1,
                                                     ddfdxdx, iterationIndexOp,
                                                     ifElses);
            }
        }

        template<class Base>
        inline void generateLoopForJacHes(ADFun<CG<Base> >& fun,
                                          const vector<CG<Base> >& x,
                                          const vector<vector<CG<Base> > >& vw,
                                          vector<CG<Base> >& y,
                                          const vector<std::set<size_t> >& jacSparsity,
                                          const vector<std::set<size_t> >& jacEvalSparsity,
                                          vector<std::map<size_t, CG<Base> > >& jac,
                                          const vector<std::set<size_t> >& hesSparsity,
                                          const vector<std::set<size_t> >& hesEvalSparsity,
                                          vector<std::map<size_t, std::map<size_t, CG<Base> > > >& vhess,
                                          bool individualColoring) {
            using namespace std;
            using namespace CppAD::extra;
            using CppAD::vector;

            typedef CG<Base> CGB;

            size_t m = fun.Range();
            size_t n = fun.Domain();

            jac.resize(m);
            vhess.resize(vw.size());

            if (!individualColoring) {
                /**
                 * No atomics
                 */

                // Jacobian for temporaries
                std::vector<size_t> jacRow, jacCol;
                generateSparsityIndexes(jacEvalSparsity, jacRow, jacCol);

                // Jacobian for equations outside loops
                vector<CGB> jacFlat(jacRow.size());

                /**
                 * Hessian - temporary variables
                 */
                std::vector<size_t> hesRow, hesCol;
                generateSparsityIndexes(hesEvalSparsity, hesRow, hesCol);

                vector<vector<CGB> > vhessFlat(vw.size());
                for (size_t l = 0; l < vw.size(); l++) {
                    vhessFlat[l].resize(hesRow.size());
                }

                vector<CG<Base> > xl;
                if (x.size() == 0) {
                    xl.resize(1); // does not depend on any variable but CppAD requires at least one
                    xl[0] = Base(0);
                } else {
                    xl = x;
                }

                SparseForjacHessianWork work;
                sparseForJacHessian(fun, xl, vw,
                                    y,
                                    jacSparsity,
                                    jacRow, jacCol, jacFlat,
                                    hesSparsity,
                                    hesRow, hesCol, vhessFlat,
                                    work);

                // save Jacobian
                for (size_t el = 0; el < jacRow.size(); el++) {
                    size_t i = jacRow[el];
                    size_t j = jacCol[el];

                    jac[i][j] = jacFlat[el];
                }

                // save Hessian
                for (size_t l = 0; l < vw.size(); l++) {
                    vector<CGB>& hessFlat = vhessFlat[l];
                    map<size_t, map<size_t, CGB> >& hess = vhess[l];

                    for (size_t el = 0; el < hesRow.size(); el++) {
                        size_t j1 = hesRow[el];
                        size_t j2 = hesCol[el];
                        hess[j1][j2] = hessFlat[el];
                    }
                }

            } else {
                /**
                 * Contains atomics
                 */

                //transpose
                vector<set<size_t> > jacEvalSparsityT(n);
                transposePattern(jacEvalSparsity, jacEvalSparsityT);

                vector<CGB> tx1v(n);

                for (size_t j1 = 0; j1 < n; j1++) {
                    if (jacEvalSparsityT[j1].empty() && hesEvalSparsity[j1].empty()) {
                        continue;
                    }

                    y = fun.Forward(0, x);

                    tx1v[j1] = Base(1);
                    vector<CGB> dy = fun.Forward(1, tx1v);
                    assert(dy.size() == m);
                    tx1v[j1] = Base(0);

                    // save Jacobian
                    const set<size_t>& column = jacEvalSparsityT[j1];
                    set<size_t>::const_iterator itI;
                    for (itI = column.begin(); itI != column.end(); ++itI) {
                        size_t i = *itI;
                        jac[i][j1] = dy[i];
                    }

                    const set<size_t>& hesRow = hesEvalSparsity[j1];

                    if (!hesRow.empty()) {

                        for (size_t l = 0; l < vw.size(); l++) {

                            vector<CGB> px = fun.Reverse(2, vw[l]);
                            assert(px.size() == 2 * n);

                            // save Hessian
                            map<size_t, CGB>& hessRow = vhess[l][j1];
                            set<size_t>::const_iterator itj2;
                            for (itj2 = hesRow.begin(); itj2 != hesRow.end(); ++itj2) {
                                size_t j2 = *itj2;
                                hessRow[j2] = px[j2 * 2 + 1];
                            }
                        }
                    }
                }
            }

        }

    }
}

#endif
