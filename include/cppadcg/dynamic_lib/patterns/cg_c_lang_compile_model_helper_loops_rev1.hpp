#ifndef CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_REV1_INCLUDED
#define CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_REV1_INCLUDED
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

    /**
     * 
     * @param sources maps files names to the generated source code
     * @param elements  [equation]{vars}
     */
    template<class Base>
    void CLangCompileModelHelper<Base>::prepareSparseReverseOneWithLoops(std::map<std::string, std::string>& sources,
                                                                         const std::map<size_t, std::vector<size_t> >& elements) throw (CGException) {
        using namespace std;
        using namespace CppAD::loops;
        using namespace CppAD::extra;
        using CppAD::vector;
        //printSparsityPattern(_jacSparsity.rows, _jacSparsity.cols, "jacobian", _fun->Range());

        size_t n = _fun.Domain();

        IndexDclrOperationNode<Base> indexJrowDcl("jrow");
        IndexDclrOperationNode<Base> indexIterationDcl(LoopModel<Base>::ITERATION_INDEX_NAME);
        IndexOperationNode<Base> iterationIndexOp(indexIterationDcl);
        IndexOperationNode<Base> jrowIndexOp(indexJrowDcl);

        std::vector<OperationNode<Base>*> localNodes(4);
        localNodes[0] = &indexJrowDcl;
        localNodes[1] = &indexIterationDcl;
        localNodes[2] = &iterationIndexOp;
        localNodes[3] = &jrowIndexOp;

        CodeHandler<Base> handler;
        handler.setJobTimer(this);
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
        std::map<size_t, std::vector<size_t> >::const_iterator itI;
        for (itI = elements.begin(); itI != elements.end(); ++itI) {//loop dependents/equations
            size_t i = itI->first;
            const std::vector<size_t>& r = itI->second;

            for (size_t e = 0; e < r.size(); e++) { // loop variables
                rows[p] = i;
                cols[p] = r[e];
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

        CGBase py;
        handler.makeVariable(py);
        if (_x.size() > 0) {
            py.setValue(Base(1.0));
        }

        /***********************************************************************
         *        generate the operation graph
         **********************************************************************/

        /**
         * original equations outside the loops 
         */
        // temporaries (zero orders)
        vector<CGBase> tmps;

        // jacobian for temporaries
        std::vector<map<size_t, CGBase> > dzDx(_funNoLoops != NULL ? _funNoLoops->getTemporaryDependentCount() : 0);

        /*******************************************************************
         * equations NOT in loops
         ******************************************************************/

        vector<CGBase> jacNoLoop; // jacobian for equations outside loops
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
             * jacobian
             */
            vector<size_t> row, col;
            generateSparsityIndexes(noLoopEvalSparsity, row, col);
            jacNoLoop.resize(row.size());

            CppAD::sparse_jacobian_work work; // temporary structure for CPPAD
            fun.SparseJacobianReverse(x, _funNoLoops->getJacobianSparsity(), row, col, jacNoLoop, work);

            const std::vector<size_t>& dependentIndexes = _funNoLoops->getOrigDependentIndexes();
            map<size_t, vector<CGBase> > jacNl; // by row

            // prepare space for the jacobian of the original equations
            for (size_t inl = 0; inl < nonIndexdedEqSize; inl++) {
                size_t i = dependentIndexes[inl];
                jacNl[i].resize(elements.at(i).size());
            }

            for (size_t el = 0; el < row.size(); el++) {
                size_t inl = row[el];
                size_t j = col[el];
                if (inl < nonIndexdedEqSize) {
                    size_t i = dependentIndexes[inl];

                    // (dy_i/dx_v) elements from equations outside loops
                    const std::set<size_t>& locations = noLoopEvalLocations[inl][j];

                    assert(locations.size() == 1); // one jacobian element should not be placed in several locations
                    size_t e = *locations.begin();

                    vector<CGBase>& row = jacNl[i];
                    row[e] = jacNoLoop[el] * py;

                    _nonLoopRev1Elements[i].insert(e);

                } else {
                    // dz_k/dx_v (for temporary variable)
                    size_t k = inl - nonIndexdedEqSize;
                    dzDx[k][j] = jacNoLoop[el];
                }
            }

            /**
             * Create source for each variable present in equations outisde loops
             */
            typename map<size_t, vector<CGBase> >::iterator itJ;
            for (itJ = jacNl.begin(); itJ != jacNl.end(); ++itJ) {
                createReverseOneWithLoopsNL(handler, itJ->first, itJ->second, sources);
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
            const std::vector<std::vector<LoopPosition> >& dependentIndexes = lModel.getDependentIndexes();
            size_t nIterations = lModel.getIterationCount();

            // reset nodes not managed by a handler
            if (itl2Eq != loopEqInfo.begin()) {
                for (size_t j = 0; j < localNodes.size(); j++) {
                    localNodes[j]->resetHandlerCounters();
                    localNodes[j]->setColor(0);
                }
            }

            _cache.str("");
            _cache << "model (reverse one, loop " << lModel.getLoopId() << ")";
            std::string jobName = _cache.str();

            /**
             * evaluate loop model jacobian
             */
            startingJob("operation graph for '" + jobName + "'");

            vector<CGBase> indexedIndeps = createIndexedIndependents(handler, lModel, iterationIndexOp);
            vector<CGBase> xl = createLoopIndependentVector(handler, lModel, indexedIndeps, x, tmps);

            vector<size_t> row, col;
            generateSparsityIndexes(loopsEvalSparsities[&lModel], row, col);
            vector<CGBase> jacLoop(row.size());

            CppAD::sparse_jacobian_work work; // temporary structure for CppAD
            fun.SparseJacobianReverse(xl, lModel.getJacobianSparsity(), row, col, jacLoop, work);

            // organize results
            std::vector<std::map<size_t, CGBase> > dyiDxtape(lModel.getTapeDependentCount());
            for (size_t el = 0; el < jacLoop.size(); el++) {
                size_t tapeI = row[el];
                size_t tapeJ = col[el];
                dyiDxtape[tapeI][tapeJ] = jacLoop[el];
            }

            finishedJob();

            /**
             * process each equation pattern (row in the loop tape)
             */
            vector<std::pair<CGBase, IndexPattern*> > indexedLoopResults;
            for (size_t tapeI = 0; tapeI < info.size(); tapeI++) {
                const JacobianWithLoopsRowInfo& rowInfo = info[tapeI];

                size_t maxRowEls = rowInfo.indexedPositions.size() + rowInfo.nonIndexedPositions.size();
                if (maxRowEls == 0)
                    continue; // nothing to do (possibly an equation assigned to a constant value)

                /**
                 * determine iteration index through the row index
                 */
                map<size_t, size_t> jrow2It;
                std::map<size_t, std::set<size_t> >& row2position = _loopRev1Groups[&lModel][tapeI];
                for (size_t it = 0; it < nIterations; it++) {
                    size_t i = dependentIndexes[tapeI][it].original;

                    jrow2It[i] = it;
                }

                std::auto_ptr<IndexPattern> itPattern(IndexPattern::detect(jrow2It));
                IndexAssignOperationNode<Base> iterationIndexPatternOp(indexIterationDcl, *itPattern.get(), jrowIndexOp);
                iterationIndexOp.makeAssigmentDependent(iterationIndexPatternOp);

                /**
                 * generate the operation graph for this equation pattern
                 */
                std::set<size_t> allLocations;
                indexedLoopResults.resize(maxRowEls);
                size_t jacLE = 0;

                vector<IfElseInfo<Base> > ifElses;

                prepareSparseJacobianRowWithLoops(handler, lModel,
                                                  tapeI, rowInfo,
                                                  dyiDxtape, dzDx,
                                                  iterationIndexOp, ifElses,
                                                  jacLE, indexedLoopResults, allLocations);
                indexedLoopResults.resize(jacLE);

                vector<CGBase> pxCustom(indexedLoopResults.size());

                for (size_t i = 0; i < indexedLoopResults.size(); i++) {
                    const CGBase& val = indexedLoopResults[i].first * py;
                    IndexPattern* ip = indexedLoopResults[i].second;

                    pxCustom[i] = createLoopDependentFunctionResult(handler, i, val, ip, iterationIndexOp);
                }

                /**
                 * save information on: row->{compressed reverse 1 position}
                 */
                for (size_t it = 0; it < nIterations; it++) {
                    size_t i = dependentIndexes[tapeI][it].original;
                    std::set<size_t>& positions = row2position[i];

                    map<size_t, std::vector<size_t> >::const_iterator itc;
                    for (itc = rowInfo.indexedPositions.begin(); itc != rowInfo.indexedPositions.end(); ++itc) {
                        const std::vector<size_t>& positionsC = itc->second;
                        positions.insert(positionsC[it]);
                    }
                    for (itc = rowInfo.nonIndexedPositions.begin(); itc != rowInfo.nonIndexedPositions.end(); ++itc) {
                        const std::vector<size_t>& positionsC = itc->second;
                        positions.insert(positionsC[it]);
                    }
                }

                /**
                 * Generate the source code
                 */
                CLanguage<Base> langC(_baseTypeName);
                langC.setFunctionIndexArgument(indexJrowDcl);

                _cache.str("");
                std::ostringstream code;
                std::auto_ptr<VariableNameGenerator<Base> > nameGen(createVariableNameGenerator("dw", "x", "var", "array"));
                CLangDefaultHessianVarNameGenerator<Base> nameGenHess(nameGen.get(), "dy", n);

                /**
                 * Generate the source code inside the loop
                 */
                _cache.str("");
                _cache << "model (forward one, loop " << lModel.getLoopId() << ", group " << tapeI << ")";
                string jobName = _cache.str();
                handler.generateCode(code, langC, pxCustom, nameGenHess, _atomicFunctions, jobName);

                _cache.str("");
                generateFunctionNameLoopRev1(_cache, lModel, tapeI);
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
                if (tapeI + 1 < info.size() || &lModel != loopEqInfo.rbegin()->first) {
                    handler.resetNodes(); // uncolor nodes
                }
            }

        }

        /**
         * 
         */
        string functionRev1 = _name + "_" + FUNCTION_SPARSE_REVERSE_ONE;
        sources[functionRev1 + ".c"] = generateGlobalForRevWithLoopsFunctionSource(elements,
                                                                                   _loopRev1Groups, _nonLoopRev1Elements,
                                                                                   functionRev1, _name, _baseTypeName, "dep",
                                                                                   generateFunctionNameLoopRev1);
        /**
         * Sparsity
         */
        _cache.str("");
        generateSparsity1DSource2(_name + "_" + FUNCTION_REVERSE_ONE_SPARSITY, elements);
        sources[_name + "_" + FUNCTION_REVERSE_ONE_SPARSITY + ".c"] = _cache.str();
        _cache.str("");
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::createReverseOneWithLoopsNL(CodeHandler<Base>& handler,
                                                                    size_t i,
                                                                    vector<CG<Base> >& jacRow,
                                                                    std::map<std::string, std::string>& sources) {
        size_t n = _fun.Domain();

        _cache.str("");
        _cache << "model (forward one, dep " << i << ") no loop";
        const std::string jobName = _cache.str();

        CLanguage<Base> langC(_baseTypeName);
        langC.setMaxAssigmentsPerFunction(_maxAssignPerFunc, &sources);
        _cache.str("");
        _cache << _name << "_" << FUNCTION_SPARSE_REVERSE_ONE << "_noloop_dep" << i;
        langC.setGenerateFunction(_cache.str());

        std::ostringstream code;
        std::auto_ptr<VariableNameGenerator<Base> > nameGen(createVariableNameGenerator("dw", "x", "var", "array"));
        CLangDefaultHessianVarNameGenerator<Base> nameGenHess(nameGen.get(), "dy", n);

        handler.generateCode(code, langC, jacRow, nameGenHess, _atomicFunctions, jobName);

        handler.resetNodes();
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::generateFunctionNameLoopRev1(std::ostringstream& cache,
                                                                     const LoopModel<Base>& loop,
                                                                     size_t tapeI) {
        generateFunctionNameLoopRev1(cache, _name, loop, tapeI);
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::generateFunctionNameLoopRev1(std::ostringstream& cache,
                                                                     const std::string& modelName,
                                                                     const LoopModel<Base>& loop,
                                                                     size_t tapeI) {
        cache << modelName << "_" << FUNCTION_SPARSE_REVERSE_ONE <<
                "_loop" << loop.getLoopId() << "_g" << tapeI;
    }

}

#endif