#ifndef CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_FOR1_INCLUDED
#define CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_FOR1_INCLUDED
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

    /***************************************************************************
     *  Methods related with loop insertion into the operation graph
     **************************************************************************/

    template<class Base>
    void CLangCompileModelHelper<Base>::prepareSparseForwardOneWithLoops(std::map<std::string, std::string>& sources,
                                                                         const std::map<size_t, std::vector<size_t> >& elements) throw (CGException) {
        using namespace std;
        using namespace CppAD::loops;
        using namespace CppAD::extra;
        using CppAD::vector;
        //printSparsityPattern(_jacSparsity.rows, _jacSparsity.cols, "jacobian", _fun.Range());

        size_t n = _fun.Domain();


        IndexDclrOperationNode<Base> indexJcolDcl("jcol");
        IndexDclrOperationNode<Base> indexLocalItDcl("it");
        IndexDclrOperationNode<Base> indexLocalItCountDcl("itCount");
        IndexDclrOperationNode<Base> indexIterationDcl(LoopModel<Base>::ITERATION_INDEX_NAME);
        IndexOperationNode<Base> iterationIndexOp(indexIterationDcl);
        IndexOperationNode<Base> jcolIndexOp(indexJcolDcl);

        std::vector<OperationNode<Base>*> localNodes(6);
        localNodes[0] = &indexJcolDcl;
        localNodes[1] = &indexLocalItDcl;
        localNodes[2] = &indexLocalItCountDcl;
        localNodes[3] = &indexIterationDcl;
        localNodes[4] = &iterationIndexOp;
        localNodes[5] = &jcolIndexOp;

        CodeHandler<Base> handler;
        handler.setJobTimer(_jobTimer);
        handler.setZeroDependents(false);

        size_t nonIndexdedEqSize = _funNoLoops != NULL ? _funNoLoops->getOrigDependentIndexes().size() : 0;

        vector<set<size_t> > noLoopEvalSparsity;
        vector<map<size_t, set<size_t> > > noLoopEvalLocations; // tape equation -> original J -> locations
        map<LoopModel<Base>*, vector<set<size_t> > > loopsEvalSparsities;
        map<LoopModel<Base>*, std::vector<JacobianWithLoopsRowInfo> > loopEqInfo;

        size_t nnz = _jacSparsity.rows.size();
        std::vector<size_t> rows(nnz);
        std::vector<size_t> cols(nnz);
        std::vector<size_t> locations(nnz);

        size_t p = 0;
        map<size_t, std::vector<size_t> >::const_iterator itJ;
        for (itJ = elements.begin(); itJ != elements.end(); ++itJ) {//loop variables
            size_t j = itJ->first;
            const std::vector<size_t>& r = itJ->second;

            for (size_t e = 0; e < r.size(); e++) { // loop equations
                rows[p] = r[e];
                cols[p] = j;
                locations[p] = e;
                p++;
            }
        }
        assert(p == nnz);

        analyseSparseJacobianWithLoops(rows, cols, locations,
                                       noLoopEvalSparsity, noLoopEvalLocations, loopsEvalSparsities, loopEqInfo);

        vector<CGBase> x(n);
        handler.makeVariables(x);
        if (_x.size() > 0) {
            for (size_t i = 0; i < n; i++) {
                x[i].setValue(_x[i]);
            }
        }

        CGBase dx;
        handler.makeVariable(dx);
        if (_x.size() > 0) {
            dx.setValue(Base(1.0));
        }

        /***********************************************************************
         *        generate the operation graph
         **********************************************************************/

        /**
         * original equations outside the loops 
         */
        // temporaries (zero orders)
        vector<CGBase> tmps;

        // Jacobian for temporaries
        map<size_t, map<size_t, CGBase> > dzDx;

        /*******************************************************************
         * equations NOT in loops
         ******************************************************************/
        if (_funNoLoops != NULL) {
            ADFun<CGBase>& fun = _funNoLoops->getTape();

            /**
             * zero order
             */
            vector<CGBase> depNL = _funNoLoops->getTape().Forward(0, x);

            tmps.resize(depNL.size() - nonIndexdedEqSize);
            for (size_t i = 0; i < tmps.size(); i++)
                tmps[i] = depNL[nonIndexdedEqSize + i];

            /**
             * Jacobian
             */
            bool hasAtomics = isAtomicsUsed(); // TODO: improve this by checking only the current fun
            map<size_t, map<size_t, CGBase> > dydxT = generateLoopFor1Jac(fun,
                                                                          _funNoLoops->getJacobianSparsity(),
                                                                          noLoopEvalSparsity,
                                                                          x, hasAtomics);

            map<size_t, vector<CGBase> > jacNl; // by column
            typename map<size_t, map<size_t, CGBase> >::const_iterator itDydxT;
            for (itDydxT = dydxT.begin(); itDydxT != dydxT.end(); ++itDydxT) {
                size_t j = itDydxT->first;
                const map<size_t, CGBase>& dydxjT = itDydxT->second;

                // prepare space for the Jacobian of the original equations
                vector<CGBase>& col = jacNl[j];
                col.resize(elements.at(j).size());

                typename map<size_t, CGBase>::const_iterator itiv;
                for (itiv = dydxjT.begin(); itiv != dydxjT.end(); ++itiv) {
                    size_t inl = itiv->first;

                    if (inl < nonIndexdedEqSize) {
                        // (dy_i/dx_v) elements from equations outside loops
                        const set<size_t>& locations = noLoopEvalLocations[inl][j];

                        assert(locations.size() == 1); // one Jacobian element should not be placed in several locations
                        size_t e = *locations.begin();

                        col[e] = itiv->second * dx;

                        _nonLoopFor1Elements[j].insert(e);
                    } else {
                        // dz_k/dx_v (for temporary variable)
                        size_t k = inl - nonIndexdedEqSize;
                        dzDx[k][j] = itiv->second;
                    }

                }
            }

            /**
             * Create source for each variable present in equations outside loops
             */
            typename map<size_t, vector<CGBase> >::iterator itJ;
            for (itJ = jacNl.begin(); itJ != jacNl.end(); ++itJ) {
                size_t j = itJ->first;
                if (_nonLoopFor1Elements.find(j) != _nonLoopFor1Elements.end()) // make sure there are elements
                    createForwardOneWithLoopsNL(handler, j, itJ->second, sources);
            }
        }

        /***********************************************************************
         * equations in loops 
         **********************************************************************/
        typename map<LoopModel<Base>*, std::vector<JacobianWithLoopsRowInfo> >::iterator itl2Eq;
        for (itl2Eq = loopEqInfo.begin(); itl2Eq != loopEqInfo.end(); ++itl2Eq) {
            LoopModel<Base>& lModel = *itl2Eq->first;
            const std::vector<JacobianWithLoopsRowInfo>& info = itl2Eq->second;
            ADFun<CGBase>& fun = lModel.getTape();

            //size_t nIndexed = lModel.getIndexedIndepIndexes().size();
            //size_t nNonIndexed = lModel.getNonIndexedIndepIndexes().size();

            // reset nodes not managed by a handler
            if (itl2Eq != loopEqInfo.begin()) {
                for (size_t j = 0; j < localNodes.size(); j++) {
                    localNodes[j]->resetHandlerCounters();
                    localNodes[j]->setColor(0);
                }
            }


            _cache.str("");
            _cache << "model (forward one, loop " << lModel.getLoopId() << ")";
            std::string jobName = _cache.str();

            /**
             * evaluate loop model Jacobian
             */
            startingJob("'" + jobName + "'", JobTimer::GRAPH);

            vector<CGBase> indexedIndeps = createIndexedIndependents(handler, lModel, iterationIndexOp);
            vector<CGBase> xl = createLoopIndependentVector(handler, lModel, indexedIndeps, x, tmps);

            bool hasAtomics = isAtomicsUsed(); // TODO: improve this by checking only the current fun
            map<size_t, map<size_t, CGBase> > dyiDxtapeT = generateLoopFor1Jac(fun,
                                                                               lModel.getJacobianSparsity(),
                                                                               loopsEvalSparsities[&lModel],
                                                                               xl, hasAtomics);

            finishedJob();

            /*******************************************************************
             * create Jacobian column groups
             * for the contributions of the equations in loops
             ******************************************************************/
            SmartVectorPointer<JacobianColGroup<Base> > loopGroups;

            generateForOneColumnGroups(lModel, info, nnz, n, loopGroups);

            /*******************************************************************
             * generate the operation graph for each Jacobian column subgroup
             ******************************************************************/
            for (size_t g = 0; g < loopGroups.v.size(); g++) {
                JacobianColGroup<Base>& group = *loopGroups.v[g];

                /**
                 * determine if a loop should be created
                 */
                LoopStartOperationNode<Base>* loopStart = NULL;

                map<size_t, set<size_t> > localIterCount2Jcols;

                map<size_t, set<size_t> >::const_iterator itJcol2It;
                for (itJcol2It = group.jCol2Iterations.begin(); itJcol2It != group.jCol2Iterations.end(); ++itJcol2It) {
                    size_t jcol = itJcol2It->first;
                    size_t itCount = itJcol2It->second.size();
                    localIterCount2Jcols[itCount].insert(jcol);
                }

                bool createsLoop = localIterCount2Jcols.size() != 1 || // is there a different number of it
                        localIterCount2Jcols.begin()->first != 1; // is there always just on iteration?

                /**
                 * Model index pattern
                 * 
                 * detect the index pattern for the model iterations
                 * based on jcol and the local loop iteration
                 */
                map<size_t, map<size_t, size_t> > jcol2localIt2ModelIt;

                for (itJcol2It = group.jCol2Iterations.begin(); itJcol2It != group.jCol2Iterations.end(); ++itJcol2It) {
                    size_t jcol = itJcol2It->first;

                    map<size_t, size_t>& localIt2ModelIt = jcol2localIt2ModelIt[jcol];
                    size_t localIt = 0;
                    set<size_t>::const_iterator itIt;
                    for (itIt = itJcol2It->second.begin(); itIt != itJcol2It->second.end(); ++itIt, localIt++) {
                        localIt2ModelIt[localIt] = *itIt;
                    }
                }

                /**
                 * try to fit a combination of two patterns:
                 *  j = fStart(jcol) + flit(lit);
                 */
                std::auto_ptr<IndexPattern> itPattern(Plane2DIndexPattern::detectPlane2D(jcol2localIt2ModelIt));

                if (itPattern.get() == NULL) {
                    // did not match!
                    itPattern.reset(new Random2DIndexPattern(jcol2localIt2ModelIt));
                }

                /**
                 * Local iteration count pattern
                 */
                std::auto_ptr<IndexOperationNode<Base> > localIterIndexOp;
                std::auto_ptr<IndexOperationNode<Base> > localIterCountIndexOp;
                std::auto_ptr<IndexAssignOperationNode<Base> > itCountAssignOp;
                std::auto_ptr<IndexPattern> indexLocalItCountPattern;

                if (createsLoop) {
                    map<size_t, size_t> jcol2litCount;

                    map<size_t, set<size_t> >::const_iterator itJcol2Its;
                    for (itJcol2Its = group.jCol2Iterations.begin(); itJcol2Its != group.jCol2Iterations.end(); ++itJcol2Its) {
                        size_t jcol = itJcol2Its->first;
                        jcol2litCount[jcol] = itJcol2Its->second.size();
                    }

                    indexLocalItCountPattern.reset(IndexPattern::detect(jcol2litCount));

                    if (IndexPattern::isConstant(*indexLocalItCountPattern.get())) {
                        size_t itCount = group.jCol2Iterations.begin()->second.size();
                        loopStart = new LoopStartOperationNode<Base>(indexLocalItDcl, itCount);
                    } else {
                        itCountAssignOp.reset(new IndexAssignOperationNode<Base>(indexLocalItCountDcl, *indexLocalItCountPattern.get(), jcolIndexOp));
                        localIterCountIndexOp.reset(new IndexOperationNode<Base>(*itCountAssignOp.get()));
                        loopStart = new LoopStartOperationNode<Base>(indexLocalItDcl, *localIterCountIndexOp.get());
                    }
                    handler.manageOperationNodeMemory(loopStart);

                    localIterIndexOp.reset(new IndexOperationNode<Base>(*loopStart));
                }


                IndexAssignOperationNode<Base> iterationIndexPatternOp(indexIterationDcl, *itPattern.get(), &jcolIndexOp, localIterIndexOp.get());
                iterationIndexOp.makeAssigmentDependent(iterationIndexPatternOp);

                map<size_t, set<size_t> > jcol2CompressedLoc;
                std::vector<pair<CG<Base>, IndexPattern*> > indexedLoopResults;

                indexedLoopResults = generateForwardOneGroupOps(handler, lModel, info,
                                                                group, iterationIndexOp,
                                                                dx, dyiDxtapeT, dzDx,
                                                                jcol2CompressedLoc);

                _loopFor1Groups[&lModel][g] = jcol2CompressedLoc;

                LoopEndOperationNode<Base>* loopEnd = NULL;
                vector<CGBase> pxCustom;
                if (createsLoop) {
                    /**
                     * make the loop end
                     */
                    size_t assignOrAdd = 1;
                    set<IndexOperationNode<Base>*> indexesOps;
                    indexesOps.insert(&iterationIndexOp);
                    loopEnd = createLoopEnd(handler, *loopStart, indexedLoopResults, indexesOps, assignOrAdd);

                    /**
                     * move non-indexed expressions outside loop
                     */
                    moveNonIndexedOutsideLoop(handler, *loopStart, *loopEnd);

                    /**
                     * 
                     */
                    pxCustom.resize(1);
                    std::vector<size_t> info(1);
                    info[0] = 0; // must point to itself since there is only one dependent
                    std::vector<Argument<Base> > args(1);
                    args[0] = Argument<Base>(*loopEnd);
                    pxCustom[0] = handler.createCG(new OperationNode<Base> (CGDependentRefRhsOp, info, args));

                } else {
                    /**
                     * No loop required
                     */
                    pxCustom.resize(indexedLoopResults.size());
                    for (size_t i = 0; i < indexedLoopResults.size(); i++) {
                        const CGBase& val = indexedLoopResults[i].first;
                        IndexPattern* ip = indexedLoopResults[i].second;

                        pxCustom[i] = createLoopDependentFunctionResult(handler, i, val, ip, iterationIndexOp);
                    }

                }

                CLanguage<Base> langC(_baseTypeName);
                langC.setFunctionIndexArgument(indexJcolDcl);
                langC.setParameterPrecision(_parameterPrecision);

                _cache.str("");
                std::ostringstream code;
                std::auto_ptr<VariableNameGenerator<Base> > nameGen(createVariableNameGenerator("dy"));
                CLangDefaultHessianVarNameGenerator<Base> nameGenHess(nameGen.get(), "dx", n);

                /**
                 * Generate the source code inside the loop
                 */
                _cache.str("");
                _cache << "model (forward one, loop " << lModel.getLoopId() << ", group " << g << ")";
                string jobName = _cache.str();
                handler.generateCode(code, langC, pxCustom, nameGenHess, _atomicFunctions, jobName);

                _cache.str("");
                generateFunctionNameLoopFor1(_cache, lModel, g);
                std::string functionName = _cache.str();

                std::string argsDcl = langC.generateFunctionArgumentsDcl();

                _cache.str("");
                _cache << "#include <stdlib.h>\n"
                        "#include <math.h>\n"
                        "\n"
                        << CLanguage<Base>::ATOMICFUN_STRUCT_DEFINITION << "\n"
                        "\n"
                        "void " << functionName << "(" << argsDcl << ") {\n";
                nameGenHess.customFunctionVariableDeclarations(_cache);
                _cache << langC.generateIndependentVariableDeclaration() << "\n";
                _cache << langC.generateDependentVariableDeclaration() << "\n";
                _cache << langC.generateTemporaryVariableDeclaration(true) << "\n";
                nameGenHess.prepareCustomFunctionVariables(_cache);

                // code inside the loop
                _cache << code.str();

                nameGenHess.finalizeCustomFunctionVariables(_cache);
                _cache << "}\n\n";

                sources[functionName + ".c"] = _cache.str();
                _cache.str("");

                /**
                 * prepare the nodes to be reused!
                 */
                if (g + 1 < loopGroups.v.size()) {
                    handler.resetNodes(); // uncolor nodes
                }
            }

        }

        /**
         * 
         */
        string functionFor1 = _name + "_" + FUNCTION_SPARSE_FORWARD_ONE;
        sources[functionFor1 + ".c"] = generateGlobalForRevWithLoopsFunctionSource(elements,
                                                                                   _loopFor1Groups, _nonLoopFor1Elements,
                                                                                   functionFor1, _name, _baseTypeName, "indep",
                                                                                   generateFunctionNameLoopFor1);
        /**
         * Sparsity
         */
        _cache.str("");
        generateSparsity1DSource2(_name + "_" + FUNCTION_FORWARD_ONE_SPARSITY, elements);
        sources[_name + "_" + FUNCTION_FORWARD_ONE_SPARSITY + ".c"] = _cache.str();
        _cache.str("");
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::createForwardOneWithLoopsNL(CodeHandler<Base>& handler,
                                                                    size_t j,
                                                                    vector<CG<Base> >& jacCol,
                                                                    std::map<std::string, std::string>& sources) {
        size_t n = _fun.Domain();

        _cache.str("");
        _cache << "model (forward one, indep " << j << ") no loop";
        const std::string jobName = _cache.str();

        CLanguage<Base> langC(_baseTypeName);
        langC.setMaxAssigmentsPerFunction(_maxAssignPerFunc, &sources);
        langC.setParameterPrecision(_parameterPrecision);
        _cache.str("");
        _cache << _name << "_" << FUNCTION_SPARSE_FORWARD_ONE << "_noloop_indep" << j;
        langC.setGenerateFunction(_cache.str());

        std::ostringstream code;
        std::auto_ptr<VariableNameGenerator<Base> > nameGen(createVariableNameGenerator("dy"));
        CLangDefaultHessianVarNameGenerator<Base> nameGenHess(nameGen.get(), "dx", n);

        handler.generateCode(code, langC, jacCol, nameGenHess, _atomicFunctions, jobName);

        handler.resetNodes();
    }


    namespace loops {

        /**
         * Auxiliary structure
         */
        struct Forward1Jcol2Iter {
            size_t jcol;
            std::set<size_t> iterations;

            inline Forward1Jcol2Iter() {
            }

            inline Forward1Jcol2Iter(size_t col,
                                     const std::set<size_t>& iters) :
                jcol(col),
                iterations(iters) {
            }
        };

        inline bool operator<(const Forward1Jcol2Iter& l, const Forward1Jcol2Iter& r) {
            if (l.jcol < r.jcol)
                return true;
            else if (l.jcol > r.jcol)
                return false;

            return compare(l.iterations, r.iterations) == -1;
        }

        /**
         * Group of contributions to a Jacobian
         */
        template<class Base>
        class JacobianTermContrib {
        public:
            std::set<size_t> indexed;
            std::set<size_t> nonIndexed; // maximum one element
        public:

            inline bool empty() const {
                return indexed.empty() && nonIndexed.empty();
            }

            inline size_t size() const {
                return indexed.size() + nonIndexed.size();
            }
        };

        template<class Base>
        bool operator<(const JacobianTermContrib<Base>& l, const JacobianTermContrib<Base>& r) {
            int c = compare(l.indexed, r.indexed);
            if (c != 0) return c == -1;
            c = compare(l.nonIndexed, r.nonIndexed);
            if (c != 0) return c == -1;
            return false;
        }

        /**
         * Group of contributions to a Jacobian with the same relation between
         * Jacobian columns and set of iterations
         */
        template<class Base>
        class JacobianColGroup : public JacobianTermContrib<Base> {
        public:
            // all the required iterations for each jcol
            std::map<size_t, std::set<size_t> > jCol2Iterations;
            // all iterations
            std::set<size_t> iterations;
            // if-else branches
            vector<IfElseInfo<Base> > ifElses;
        public:

            inline JacobianColGroup(const JacobianTermContrib<Base>& c,
                                    const Forward1Jcol2Iter& jcol2Iters) :
                JacobianTermContrib<Base>(c),
                iterations(jcol2Iters.iterations) {
                jCol2Iterations[jcol2Iters.jcol] = jcol2Iters.iterations;
            }
        };

        template<class Base>
        void generateForOneColumnGroups(const LoopModel<Base>& lModel,
                                        const std::vector<JacobianWithLoopsRowInfo>& loopEqInfo,
                                        size_t max,
                                        size_t n,
                                        SmartVectorPointer<JacobianColGroup<Base> >& loopGroups) {
            using namespace std;
            using namespace CppAD::loops;
            using CppAD::vector;

            /**
             * group columns with the same contribution terms
             */
            map<size_t, map<size_t, set<size_t> > > indexed2jcol2Iter;
            map<size_t, set<size_t> > nonIndexed2Iter;

            map<JacobianTermContrib<Base>, set<size_t> > contrib2jcols = groupForOneByContrib(lModel, loopEqInfo,
                                                                                              n,
                                                                                              indexed2jcol2Iter,
                                                                                              nonIndexed2Iter);

            loopGroups.v.reserve(contrib2jcols.size() * 2); // TODO: improve this
            std::map<JacobianTermContrib<Base>, JacobianColGroup<Base>*> c2subgroups;

            typename map<JacobianTermContrib<Base>, set<size_t> >::const_iterator itC;
            for (itC = contrib2jcols.begin(); itC != contrib2jcols.end(); ++itC) {
                const JacobianTermContrib<Base>& c = itC->first;
                const set<size_t>& jcols = itC->second;

                /**
                 * create subgroups
                 */
                subgroupForOneByContrib(loopEqInfo, c, jcols,
                                        indexed2jcol2Iter, nonIndexed2Iter,
                                        loopGroups, c2subgroups);
            }

        }

        /**
         * Create groups with the same contributions at the same Jacobian columns
         * 
         * @return maps each contribution group to the affected columns in the
         *         Jacobian
         */
        template<class Base>
        std::map<JacobianTermContrib<Base>, std::set<size_t> > groupForOneByContrib(const LoopModel<Base>& lModel,
                                                                                    const std::vector<JacobianWithLoopsRowInfo>& loopEqInfo,
                                                                                    size_t n,
                                                                                    std::map<size_t, std::map<size_t, std::set<size_t> > >& indexed2jcol2Iter,
                                                                                    std::map<size_t, std::set<size_t> >& nonIndexed2Iter) {

            using namespace std;

            /**
             * determine the contributions to each Jacobian column
             */
            std::vector<JacobianTermContrib<Base> > jcols(n);

            size_t nIterations = lModel.getIterationCount();

            const std::vector<std::vector<LoopPosition> >& indexedIndepIndexes = lModel.getIndexedIndepIndexes();

            for (size_t i = 0; i < loopEqInfo.size(); i++) {
                const JacobianWithLoopsRowInfo& row = loopEqInfo[i];

                map<size_t, std::vector<size_t> >::const_iterator it;
                // indexed
                for (it = row.indexedPositions.begin(); it != row.indexedPositions.end(); ++it) {
                    size_t tapeJ = it->first;
                    const std::vector<size_t>& positions = it->second;
                    map<size_t, set<size_t> >& jcol2Iter = indexed2jcol2Iter[tapeJ];

                    for (size_t iter = 0; iter < nIterations; iter++) {
                        // it is present in all iterations but the user might request fewer elements in the Jacobian
                        // (it may be because the equation might not exist for this iteration)
                        if (positions[iter] != std::numeric_limits<size_t>::max()) {
                            size_t j = indexedIndepIndexes[tapeJ][iter].original;
                            jcols[j].indexed.insert(tapeJ);
                            jcol2Iter[j].insert(iter);
                        }
                    }
                }

                // non-indexed
                for (it = row.nonIndexedPositions.begin(); it != row.nonIndexedPositions.end(); ++it) {
                    size_t j = it->first;
                    const std::vector<size_t>& positions = it->second;
                    set<size_t>& jcol2Iter = nonIndexed2Iter[j];
                    bool used = false;

                    for (size_t iter = 0; iter < nIterations; iter++) {
                        // it is present in all iterations but the user might request fewer elements in the Jacobian
                        // (it may be because the equation might not exist for this iteration)
                        if (positions[iter] != std::numeric_limits<size_t>::max()) {
                            used = true;
                            jcol2Iter.insert(iter);
                        }
                    }
                    if (used) {
                        jcols[j].nonIndexed.insert(j);
                    }
                }

            }

            /**
             * group columns with the same contribution terms
             */
            map<JacobianTermContrib<Base>, set<size_t> > contrib2jcols;
            for (size_t j = 0; j < n; j++) {
                if (!jcols[j].empty())
                    contrib2jcols[jcols[j]].insert(j);
            }

            return contrib2jcols;
        }

        /**
         * Create subgroups from groups with the same contributions at the 
         * same Jacobian columns. Each subgroup has a sub-set of the group's 
         * contributions which have the same relations between Jacobian column
         * index and set of iteration indexes.
         */
        template<class Base>
        inline void subgroupForOneByContrib(const std::vector<JacobianWithLoopsRowInfo>& loopEqInfo,
                                            const JacobianTermContrib<Base>& c,
                                            const std::set<size_t>& jcols,
                                            const std::map<size_t, std::map<size_t, std::set<size_t> > >& indexed2jcol2Iter,
                                            const std::map<size_t, std::set<size_t> >& nonIndexed2Iter,
                                            SmartVectorPointer<JacobianColGroup<Base> >& subGroups,
                                            std::map<JacobianTermContrib<Base>, JacobianColGroup<Base>*>& c2subgroups) {
            using namespace std;

            map<Forward1Jcol2Iter, JacobianTermContrib<Base> > contribs;

            set<size_t>::const_iterator it;
            map<size_t, set<size_t> >::const_iterator itJcol2Iter;

            //  indexed
            for (it = c.indexed.begin(); it != c.indexed.end(); ++it) {
                size_t tapeJ = *it;

                map<size_t, set<size_t> > jcol2Iters = filterBykeys(indexed2jcol2Iter.at(tapeJ), jcols);
                for (itJcol2Iter = jcol2Iters.begin(); itJcol2Iter != jcol2Iters.end(); ++itJcol2Iter) {
                    Forward1Jcol2Iter k(itJcol2Iter->first, itJcol2Iter->second);
                    contribs[k].indexed.insert(tapeJ);
                }
            }

            // non-indexed
            for (it = c.nonIndexed.begin(); it != c.nonIndexed.end(); ++it) {
                size_t j = *it;

                // probably present in all iterations but the user might request fewer elements in the Jacobian
                // (it may be because the equation might not exist for this iteration)
                const set<size_t>& iters = nonIndexed2Iter.at(j);
                Forward1Jcol2Iter k(j, iters);
                contribs[k].nonIndexed.insert(j);
            }

            /**
             * 
             */
            typename map<Forward1Jcol2Iter, JacobianTermContrib<Base> >::const_iterator itK2C;
            for (itK2C = contribs.begin(); itK2C != contribs.end(); ++itK2C) {
                const Forward1Jcol2Iter& jcol2Iters = itK2C->first;
                const JacobianTermContrib<Base>& hc = itK2C->second;

                typename map<JacobianTermContrib<Base>, JacobianColGroup<Base>*>::const_iterator its = c2subgroups.find(hc);
                if (its != c2subgroups.end()) {
                    JacobianColGroup<Base>* sg = its->second;
                    sg->jCol2Iterations[jcol2Iters.jcol] = jcol2Iters.iterations;
                    sg->iterations.insert(jcol2Iters.iterations.begin(), jcol2Iters.iterations.end());
                } else {
                    JacobianColGroup<Base>* sg = new JacobianColGroup<Base>(hc, jcol2Iters);
                    subGroups.v.push_back(sg);
                    c2subgroups[hc] = sg;
                }
            }
        }

        template<class Base>
        std::vector<std::pair<CG<Base>, IndexPattern*> > generateForwardOneGroupOps(CodeHandler<Base>& handler,
                                                                                    const LoopModel<Base>& lModel,
                                                                                    const std::vector<JacobianWithLoopsRowInfo>& info,
                                                                                    JacobianColGroup<Base>& group,
                                                                                    IndexOperationNode<Base>& iterationIndexOp,
                                                                                    const CG<Base>& dx,
                                                                                    const std::map<size_t, std::map<size_t, CG<Base> > >& dyiDxtapeT,
                                                                                    const std::map<size_t, std::map<size_t, CG<Base> > >& dzDx,
                                                                                    std::map<size_t, std::set<size_t> >& jcol2CompressedLoc) {
            using namespace std;
            using namespace CppAD::loops;
            using CppAD::vector;

            typedef CG<Base> CGBase;

            const std::vector<std::vector<LoopPosition> >& indexedIndepIndexes = lModel.getIndexedIndepIndexes();

            size_t jacElSize = group.size();

            std::vector<pair<CGBase, IndexPattern*> > indexedLoopResults(jacElSize * info.size());
            size_t jacLE = 0;

            map<size_t, size_t> iter2jcols;

            set<size_t>::const_iterator itIt;

            for (size_t tapeI = 0; tapeI < info.size(); tapeI++) {
                const JacobianWithLoopsRowInfo& jlrw = info[tapeI];

                /**
                 * indexed variable contributions
                 */
                // tape J index -> {locationIt0, locationIt1, ...}
                set<size_t>::iterator itJ;
                for (itJ = group.indexed.begin(); itJ != group.indexed.end(); ++itJ) {
                    size_t tapeJ = *itJ;

                    map<size_t, std::vector<size_t> >::const_iterator itPos = jlrw.indexedPositions.find(tapeJ);
                    if (itPos != jlrw.indexedPositions.end()) {
                        const std::vector<size_t>& positions = itPos->second; // compressed positions

                        const std::vector<LoopPosition>& tapeJPos = indexedIndepIndexes[tapeJ];
                        iter2jcols.clear();
                        for (itIt = group.iterations.begin(); itIt != group.iterations.end(); ++itIt) {
                            size_t iter = *itIt;
                            if (positions[iter] != std::numeric_limits<size_t>::max()) { // the element must have been requested
                                assert(tapeJPos[iter].original != std::numeric_limits<size_t>::max()); // the equation must exist for this iteration
                                iter2jcols[iter] = tapeJPos[iter].original;
                            }
                        }

                        if (!iter2jcols.empty()) {
                            CGBase val = dyiDxtapeT.at(tapeJ).at(tapeI) * dx;
                            indexedLoopResults[jacLE++] = createForwardOneElement(handler, group, positions, iter2jcols,
                                                                                  val, iterationIndexOp, jcol2CompressedLoc);
                        }
                    }
                }


                /**
                 * non-indexed variable contributions
                 */
                // original J index -> {locationIt0, locationIt1, ...}
                for (itJ = group.nonIndexed.begin(); itJ != group.nonIndexed.end(); ++itJ) {
                    size_t j = *itJ;

                    map<size_t, std::vector<size_t> >::const_iterator itPos = jlrw.nonIndexedPositions.find(j);
                    if (itPos == jlrw.nonIndexedPositions.end()) {
                        continue;
                    }
                    const std::vector<size_t>& positions = itPos->second;

                    iter2jcols.clear();
                    for (itIt = group.iterations.begin(); itIt != group.iterations.end(); ++itIt) {
                        size_t iter = *itIt;
                        if (positions[iter] != std::numeric_limits<size_t>::max()) {// the element must have been requested
                            assert(lModel.getDependentIndexes()[tapeI][iter].original != std::numeric_limits<size_t>::max()); // the equation must exist for this iteration
                            iter2jcols[iter] = j;
                        }
                    }
                    if (!iter2jcols.empty()) {

                        CGBase jacVal = Base(0);

                        // non-indexed variables used directly
                        const LoopPosition* pos = lModel.getNonIndexedIndepIndexes(j);
                        if (pos != NULL) {
                            size_t tapeJ = pos->tape;

                            const map<size_t, CG<Base> >& dyiDxJtapeT = dyiDxtapeT.at(tapeJ);
                            typename map<size_t, CGBase>::const_iterator itVal = dyiDxJtapeT.find(tapeI);
                            if (itVal != dyiDxJtapeT.end()) {
                                jacVal += itVal->second;
                            }
                        }

                        // non-indexed variables used through temporary variables
                        map<size_t, set<size_t> >::const_iterator itks = jlrw.tmpEvals.find(j);
                        if (itks != jlrw.tmpEvals.end()) {
                            const set<size_t>& ks = itks->second;
                            set<size_t>::const_iterator itk;
                            for (itk = ks.begin(); itk != ks.end(); ++itk) {
                                size_t k = *itk;
                                size_t tapeJ = lModel.getTempIndepIndexes(k)->tape;

                                jacVal += dyiDxtapeT.at(tapeJ).at(tapeI) * dzDx.at(k).at(j);
                            }
                        }

                        CGBase val = jacVal * dx;
                        indexedLoopResults[jacLE++] = createForwardOneElement(handler, group, positions, iter2jcols,
                                                                              val, iterationIndexOp, jcol2CompressedLoc);
                    }
                }
            }

            indexedLoopResults.resize(jacLE);

            return indexedLoopResults;
        }

        template<class Base>
        std::pair<CG<Base>, IndexPattern*> createForwardOneElement(CodeHandler<Base>& handler,
                                                                   JacobianColGroup<Base>& group,
                                                                   const std::vector<size_t>& positions,
                                                                   const std::map<size_t, size_t>& iter2jcols,
                                                                   const CG<Base>& dfdx,
                                                                   IndexOperationNode<Base>& iterationIndexOp,
                                                                   std::map<size_t, std::set<size_t> >& jcol2CompressedLoc) {
            using namespace std;

            /**
             * Determine index pattern
             */
            map<size_t, size_t> locationsIter2Pos;

            std::map<size_t, size_t>::const_iterator itIt;
            for (itIt = iter2jcols.begin(); itIt != iter2jcols.end(); ++itIt) {
                size_t iter = itIt->first;
                size_t jcol = itIt->second;
                assert(positions[iter] != std::numeric_limits<size_t>::max());
                locationsIter2Pos[iter] = positions[iter];
                jcol2CompressedLoc[jcol].insert(positions[iter]);
            }

            // generate the index pattern for the Jacobian compressed element
            IndexPattern* pattern = IndexPattern::detect(locationsIter2Pos);
            handler.manageLoopDependentIndexPattern(pattern);

            size_t assignOrAdd = 1;
            return createLoopResult(handler, locationsIter2Pos, positions.size(),
                                    dfdx, pattern, assignOrAdd,
                                    iterationIndexOp, group.ifElses);
        }

    }

    template<class Base>
    std::map<size_t, std::map<size_t, CG<Base> > > CLangCompileModelHelper<Base>::generateLoopFor1Jac(ADFun<CGBase>& fun,
                                                                                                      const vector<std::set<size_t> >& sparsity,
                                                                                                      const vector<std::set<size_t> >& evalSparsity,
                                                                                                      const vector<CGBase>& x,
                                                                                                      bool constainsAtomics) {
        using namespace std;
        using namespace CppAD::extra;
        using CppAD::vector;

        size_t n = fun.Domain();

        map<size_t, map<size_t, CGBase> > dyDxT;

        if (!constainsAtomics) {
            vector<size_t> row, col;
            generateSparsityIndexes(evalSparsity, row, col);

            if (row.size() == 0)
                return dyDxT; // nothing to do

            vector<CGBase> jacLoop(row.size());

            CppAD::sparse_jacobian_work work; // temporary structure for CppAD
            fun.SparseJacobianForward(x, sparsity, row, col, jacLoop, work);

            // organize results
            for (size_t el = 0; el < jacLoop.size(); el++) {
                size_t i = row[el];
                size_t j = col[el];
                dyDxT[j][i] = jacLoop[el];
            }

        } else {
            //transpose
            vector<set<size_t> > evalSparsityT(n);
            transposePattern(evalSparsity, evalSparsityT);

            vector<CGBase> dx(n);

            for (size_t j = 0; j < n; j++) {
                const set<size_t>& column = evalSparsityT[j];

                if (column.empty())
                    continue;

                fun.Forward(0, x);

                dx[j] = Base(1);
                vector<CGBase> dy = fun.Forward(1, dx);
                assert(dy.size() == fun.Range());
                dx[j] = Base(0);

                map<size_t, CGBase>& dyDxJT = dyDxT[j];

                set<size_t>::const_iterator it2;
                for (it2 = column.begin(); it2 != column.end(); ++it2) {
                    size_t i = *it2;
                    dyDxJT[i] = dy[i];
                }
            }

        }

        return dyDxT;
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::generateFunctionNameLoopFor1(std::ostringstream& cache,
                                                                     const LoopModel<Base>& loop,
                                                                     size_t g) {
        generateFunctionNameLoopFor1(cache, _name, loop, g);
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::generateFunctionNameLoopFor1(std::ostringstream& cache,
                                                                     const std::string& modelName,
                                                                     const LoopModel<Base>& loop,
                                                                     size_t g) {
        cache << modelName << "_" << FUNCTION_SPARSE_FORWARD_ONE <<
                "_loop" << loop.getLoopId() << "_g" << g;
    }

}

#endif