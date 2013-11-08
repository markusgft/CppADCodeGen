#ifndef CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_JAC_INCLUDED
#define CPPAD_CG_C_LANG_COMPILE_MODEL_HELPER_LOOPS_JAC_INCLUDED
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

        template<class Base>
        std::pair<CG<Base>, IndexPattern*> createJacobianElement(CodeHandler<Base>& handler,
                                                                 const std::vector<size_t>& positions,
                                                                 size_t maxPos,
                                                                 const CG<Base>& dfdx,
                                                                 IndexOperationNode<Base>& iterationIndexOp,
                                                                 vector<IfElseInfo<Base> >& ifElses,
                                                                 std::set<size_t>& allLocations);

    }

    /***************************************************************************
     *  Methods related with loop insertion into the operation graph
     **************************************************************************/

    template<class Base>
    void CLangCompileModelHelper<Base>::analyseSparseJacobianWithLoops(const std::vector<size_t>& rows,
                                                                       const std::vector<size_t>& cols,
                                                                       const std::vector<size_t>& location,
                                                                       vector<std::set<size_t> >& noLoopEvalSparsity,
                                                                       vector<std::map<size_t, std::set<size_t> > >& noLoopEvalLocations,
                                                                       std::map<LoopModel<Base>*, vector<std::set<size_t> > >& loopsEvalSparsities,
                                                                       std::map<LoopModel<Base>*, std::vector<loops::JacobianWithLoopsRowInfo> >& loopEqInfo) throw (CGException) {

        using namespace std;
        using namespace loops;
        using CppAD::vector;

        assert(rows.size() == cols.size());
        assert(rows.size() == location.size());

        /**
         * determine sparsities
         */
        typename std::set<LoopModel<Base>*>::const_iterator itloop;
        for (itloop = _loopTapes.begin(); itloop != _loopTapes.end(); ++itloop) {
            LoopModel<Base>* l = *itloop;
            l->evalJacobianSparsity();
        }

        if (_funNoLoops != NULL)
            _funNoLoops->evalJacobianSparsity();

        /**
         * Generate index patterns for the jacobian elements resulting from loops
         */
        size_t nonIndexdedEqSize = _funNoLoops != NULL ? _funNoLoops->getOrigDependentIndexes().size() : 0;
        noLoopEvalSparsity.resize(_funNoLoops != NULL ? _funNoLoops->getTapeDependentCount() : 0);

        // tape equation -> original J -> locations
        noLoopEvalLocations.resize(noLoopEvalSparsity.size());

        // loop -> equation -> row info
        for (itloop = _loopTapes.begin(); itloop != _loopTapes.end(); ++itloop) {
            LoopModel<Base>* loop = *itloop;
            loopEqInfo[loop].resize(loop->getTapeDependentCount());
            loopsEvalSparsities[loop].resize(loop->getTapeDependentCount());
        }

        size_t nnz = rows.size();

        /** 
         * Load locations in the compressed jacobian
         */
        for (size_t el = 0; el < nnz; el++) {
            size_t i = rows[el];
            size_t j = cols[el];
            size_t e = location[el];

            // find LOOP + get loop results
            LoopModel<Base>* loop = NULL;

            size_t tapeI;
            size_t iteration;

            for (itloop = _loopTapes.begin(); itloop != _loopTapes.end(); ++itloop) {
                LoopModel<Base>* l = *itloop;
                const std::map<size_t, LoopIndexedPosition>& depIndexes = l->getOriginalDependentIndexes();
                std::map<size_t, LoopIndexedPosition>::const_iterator iti = depIndexes.find(i);
                if (iti != depIndexes.end()) {
                    loop = l;
                    tapeI = iti->second.tape;
                    iteration = iti->second.iteration;
                    break;
                }
            }

            if (loop == NULL) {
                /**
                 * Equation present in the model without loops
                 */
                assert(_funNoLoops != NULL);
                size_t il = _funNoLoops->getLocalDependentIndex(i);

                noLoopEvalSparsity[il].insert(j);
                noLoopEvalLocations[il][j].insert(e);

            } else {
                /**
                 * Equation belongs to a loop
                 */
                size_t iterations = loop->getIterationCount();

                const std::vector<std::vector<LoopPosition> >& indexedIndepIndexes = loop->getIndexedIndepIndexes();
                const std::vector<LoopPosition>& nonIndexedIndepIndexes = loop->getNonIndexedIndepIndexes();
                const std::vector<LoopPosition>& temporaryIndependents = loop->getTemporaryIndependents();

                size_t nIndexed = indexedIndepIndexes.size();
                size_t nNonIndexed = nonIndexedIndepIndexes.size();

                const vector<std::set<size_t> >& loopSparsity = loop->getJacobianSparsity();
                const std::set<size_t>& loopRow = loopSparsity[tapeI];

                JacobianWithLoopsRowInfo& rowInfo = loopEqInfo[loop][tapeI];

                std::set<size_t>& loopEvalRow = loopsEvalSparsities[loop][tapeI];

                /**
                 * find if there are indexed variables in this equation pattern
                 * and iteration which use j
                 */
                const std::set<size_t>& tapeJs = loop->getIndexedTapeIndexes(iteration, j);
                std::set<size_t>::const_iterator itTJ;
                for (itTJ = tapeJs.begin(); itTJ != tapeJs.end(); ++itTJ) {
                    size_t tapeJ = *itTJ;
                    if (loopRow.find(tapeJ) != loopRow.end()) {
                        loopEvalRow.insert(tapeJ);

                        //this indexed variable must be request for all iterations 
                        std::vector<size_t>& positions = rowInfo.indexedPositions[tapeJ];
                        positions.resize(iterations, nnz);
                        if (positions[iteration] != nnz) {
                            std::ostringstream ss;
                            ss << "Repeated jacobian elements requested (equation " << i << ", variable " << j << ")";
                            throw CGException(ss.str());
                        }
                        positions[iteration] = e;
                    }
                }

                /**
                 * find if there is a non indexed variable in this equation pattern for j
                 */
                const LoopPosition* pos = loop->getNonIndexedIndepIndexes(j);
                bool jInNonIndexed = false;
                if (pos != NULL && loopRow.find(pos->tape) != loopRow.end()) {
                    loopEvalRow.insert(pos->tape);

                    //this non-indexed element must be request for all iterations 
                    std::vector<size_t>& positions = rowInfo.nonIndexedPositions[j];
                    positions.resize(iterations, nnz);
                    if (positions[iteration] != nnz) {
                        std::ostringstream ss;
                        ss << "Repeated jacobian elements requested (equation " << i << ", variable " << j << ")";
                        throw CGException(ss.str());
                    }
                    positions[iteration] = e;

                    rowInfo.nonIndexedEvals.insert(j);

                    jInNonIndexed = true;
                }

                /**
                 * find temporary variables used by this equation pattern
                 */
                if (_funNoLoops != NULL) {
                    set<size_t>::const_iterator itz = loopRow.lower_bound(nIndexed + nNonIndexed);

                    // loop temporary variables
                    for (; itz != loopRow.end(); ++itz) {
                        size_t tapeJ = *itz;
                        size_t k = temporaryIndependents[tapeJ - nIndexed - nNonIndexed].original;

                        /**
                         * check if this temporary depends on j
                         */
                        bool used = false;
                        const set<size_t>& sparsity = _funNoLoops->getJacobianSparsity()[nonIndexdedEqSize + k];
                        if (sparsity.find(j) != sparsity.end()) {
                            noLoopEvalSparsity[nonIndexdedEqSize + k].insert(j); // element required
                            if (!jInNonIndexed) {
                                std::vector<size_t>& positions = rowInfo.nonIndexedPositions[j];
                                positions.resize(iterations, nnz);
                                if (positions[iteration] != nnz) {
                                    std::ostringstream ss;
                                    ss << "Repeated jacobian elements requested (equation " << i << ", variable " << j << ")";
                                    throw CGException(ss.str());
                                }
                                positions[iteration] = e;
                                jInNonIndexed = true;
                            }
                            rowInfo.tmpEvals[j].insert(k);
                            used = true;
                        }

                        if (used) {
                            // this temporary variable should be evaluated
                            loopEvalRow.insert(tapeJ);
                        }
                    }
                }

            }
        }
    }

    template<class Base>
    vector<CG<Base> > CLangCompileModelHelper<Base>::prepareSparseJacobianWithLoops(CodeHandler<Base>& handler,
                                                                                    const vector<CGBase>& x,
                                                                                    bool forward) throw (CGException) {
        using namespace std;
        using namespace CppAD::loops;
        using namespace CppAD::extra;
        using CppAD::vector;
        //printSparsityPattern(_jacSparsity.rows, _jacSparsity.cols, "jacobian", _fun->Range());

        handler.setZeroDependents(true);

        size_t nonIndexdedEqSize = _funNoLoops != NULL ? _funNoLoops->getOrigDependentIndexes().size() : 0;

        vector<set<size_t> > noLoopEvalSparsity;
        vector<map<size_t, set<size_t> > > noLoopEvalLocations; // tape equation -> original J -> locations
        map<LoopModel<Base>*, vector<set<size_t> > > loopsEvalSparsities;
        map<LoopModel<Base>*, std::vector<JacobianWithLoopsRowInfo> > loopEqInfo;

        size_t nnz = _jacSparsity.rows.size();
        std::vector<size_t> locations(nnz);
        for (size_t e = 0; e < nnz; e++)
            locations[e] = e;

        analyseSparseJacobianWithLoops(_jacSparsity.rows, _jacSparsity.cols, locations,
                                       noLoopEvalSparsity, noLoopEvalLocations, loopsEvalSparsities, loopEqInfo);


        vector<CGBase> jac(nnz);

        /***********************************************************************
         *        generate the operation graph
         **********************************************************************/

        /**
         * original equations outside the loops 
         */
        // temporaries (zero orders)
        vector<CGBase> tmps;

        // Jacobian for temporaries
        std::vector<map<size_t, CGBase> > dzDx(_funNoLoops != NULL ? _funNoLoops->getTemporaryDependentCount() : 0);

        // Jacobian for equations outside loops
        vector<CGBase> jacNoLoop;
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
            if (forward) {
                fun.SparseJacobianForward(x, _funNoLoops->getJacobianSparsity(), row, col, jacNoLoop, work);
            } else {
                fun.SparseJacobianReverse(x, _funNoLoops->getJacobianSparsity(), row, col, jacNoLoop, work);
            }

            for (size_t el = 0; el < row.size(); el++) {
                size_t il = row[el];
                size_t j = col[el];
                if (il < nonIndexdedEqSize) {
                    // (dy_i/dx_v) elements from equations outside loops
                    const std::set<size_t>& locations = noLoopEvalLocations[il][j];
                    for (std::set<size_t>::const_iterator itE = locations.begin(); itE != locations.end(); ++itE)
                        jac[*itE] = jacNoLoop[el];
                } else {
                    // dz_k/dx_v (for temporary variable)
                    size_t k = il - nonIndexdedEqSize;
                    dzDx[k][j] = jacNoLoop[el];
                }
            }
        }

        /***********************************************************************
         * Generate loop body
         **********************************************************************/
        IndexDclrOperationNode<Base>* iterationIndexDcl = new IndexDclrOperationNode<Base>(LoopModel<Base>::ITERATION_INDEX_NAME);
        handler.manageOperationNodeMemory(iterationIndexDcl);

        vector<CGBase> jacLoop;

        // loop loops :)
        typename map<LoopModel<Base>*, std::vector<JacobianWithLoopsRowInfo> >::iterator itl2Eq;
        for (itl2Eq = loopEqInfo.begin(); itl2Eq != loopEqInfo.end(); ++itl2Eq) {
            LoopModel<Base>& lModel = *itl2Eq->first;
            std::vector<JacobianWithLoopsRowInfo>& eqs = itl2Eq->second;
            ADFun<CGBase>& fun = lModel.getTape();

            vector<IfElseInfo<Base> > ifElses;

            /**
             * make the loop start
             */
            LoopStartOperationNode<Base>* loopStart = new LoopStartOperationNode<Base>(*iterationIndexDcl, lModel.getIterationCount());
            handler.manageOperationNodeMemory(loopStart);

            IndexOperationNode<Base>* iterationIndexOp = new IndexOperationNode<Base>(*loopStart);
            handler.manageOperationNodeMemory(iterationIndexOp);
            std::set<IndexOperationNode<Base>*> indexesOps;
            indexesOps.insert(iterationIndexOp);

            /**
             * evaluate loop model Jacobian
             */
            vector<CGBase> indexedIndeps = createIndexedIndependents(handler, lModel, *iterationIndexOp);
            vector<CGBase> xl = createLoopIndependentVector(handler, lModel, indexedIndeps, x, tmps);

            vector<size_t> row, col;
            generateSparsityIndexes(loopsEvalSparsities[&lModel], row, col);
            jacLoop.resize(row.size());

            if (row.size() == 0) {
                continue;
            }

            CppAD::sparse_jacobian_work work; // temporary structure for CppAD
            if (forward) {
                fun.SparseJacobianForward(xl, lModel.getJacobianSparsity(), row, col, jacLoop, work);
            } else {
                fun.SparseJacobianReverse(xl, lModel.getJacobianSparsity(), row, col, jacLoop, work);
            }

            // organize results
            std::vector<std::map<size_t, CGBase> > dyiDxtape(lModel.getTapeDependentCount());
            for (size_t el = 0; el < jacLoop.size(); el++) {
                size_t tapeI = row[el];
                size_t tapeJ = col[el];
                dyiDxtape[tapeI][tapeJ] = jacLoop[el];
            }

            // all assigned elements in the compressed Jacobian by this loop
            std::set<size_t> allLocations;

            // store results in indexedLoopResults
            size_t maxJacElSize = 0;
            for (size_t tapeI = 0; tapeI < eqs.size(); tapeI++) {
                JacobianWithLoopsRowInfo& rowInfo = eqs[tapeI];
                maxJacElSize += rowInfo.indexedPositions.size();
                maxJacElSize += rowInfo.nonIndexedPositions.size();
            }

            vector<std::pair<CGBase, IndexPattern*> > indexedLoopResults(maxJacElSize);

            // create the dependents (jac elements) for indexed and constant 
            size_t jacLE = 0;
            for (size_t tapeI = 0; tapeI < eqs.size(); tapeI++) {
                JacobianWithLoopsRowInfo& rowInfo = eqs[tapeI];

                prepareSparseJacobianRowWithLoops(handler, lModel,
                                                  tapeI, rowInfo,
                                                  dyiDxtape, dzDx,
                                                  CGBase(1),
                                                  *iterationIndexOp, ifElses,
                                                  jacLE, indexedLoopResults, allLocations);
            }

            indexedLoopResults.resize(jacLE);

            /**
             * make the loop end
             */
            size_t assignOrAdd = 1;
            LoopEndOperationNode<Base>* loopEnd = createLoopEnd(handler, *loopStart, indexedLoopResults, indexesOps, assignOrAdd);

            std::vector<size_t> info(1);
            std::vector<Argument<Base> > args(1);
            std::set<size_t>::const_iterator itE;
            for (itE = allLocations.begin(); itE != allLocations.end(); ++itE) {
                // an additional alias variable is required so that each dependent variable can have its own ID
                size_t e = *itE;
                info[0] = e;
                args[0] = Argument<Base>(*loopEnd);
                jac[e] = handler.createCG(new OperationNode<Base> (CGDependentRefRhsOp, info, args));
            }

            /**
             * move non-indexed expressions outside loop
             */
            moveNonIndexedOutsideLoop(*loopStart, *loopEnd);
        }

        return jac;
    }

    template<class Base>
    void CLangCompileModelHelper<Base>::prepareSparseJacobianRowWithLoops(CodeHandler<Base>& handler,
                                                                          LoopModel<Base>& lModel,
                                                                          size_t tapeI,
                                                                          const loops::JacobianWithLoopsRowInfo& rowInfo,
                                                                          const std::vector<std::map<size_t, CGBase> >& dyiDxtape,
                                                                          const std::vector<std::map<size_t, CGBase> >& dzDx,
                                                                          const CGBase& py,
                                                                          IndexOperationNode<Base>& iterationIndexOp,
                                                                          vector<loops::IfElseInfo<Base> >& ifElses,
                                                                          size_t& jacLE,
                                                                          vector<std::pair<CG<Base>, IndexPattern*> >& indexedLoopResults,
                                                                          std::set<size_t>& allLocations) {
        using namespace std;
        using namespace loops;
        using CppAD::vector;

        size_t nnz = _jacSparsity.rows.size();

        /**
         * indexed variable contributions
         */
        // tape J index -> {locationIt0, locationIt1, ...}
        map<size_t, std::vector<size_t> >::const_iterator itJ2Pos;
        for (itJ2Pos = rowInfo.indexedPositions.begin(); itJ2Pos != rowInfo.indexedPositions.end(); ++itJ2Pos) {
            size_t tapeJ = itJ2Pos->first;
            const std::vector<size_t>& positions = itJ2Pos->second;

            CGBase jacVal = dyiDxtape[tapeI].at(tapeJ) * py;

            indexedLoopResults[jacLE++] = createJacobianElement(handler, positions, nnz,
                                                                jacVal, iterationIndexOp, ifElses,
                                                                allLocations);
        }

        /**
         * non-indexed variable contributions
         */
        // original J index -> {locationIt0, locationIt1, ...}
        for (itJ2Pos = rowInfo.nonIndexedPositions.begin(); itJ2Pos != rowInfo.nonIndexedPositions.end(); ++itJ2Pos) {
            size_t j = itJ2Pos->first;
            const std::vector<size_t>& positions = itJ2Pos->second;

            CGBase jacVal = Base(0);

            // non-indexed variables used directly
            const LoopPosition* pos = lModel.getNonIndexedIndepIndexes(j);
            if (pos != NULL) {
                size_t tapeJ = pos->tape;
                typename std::map<size_t, CGBase>::const_iterator itVal = dyiDxtape[tapeI].find(tapeJ);
                if (itVal != dyiDxtape[tapeI].end()) {
                    jacVal += itVal->second;
                }
            }

            // non-indexed variables used through temporary variables
            std::map<size_t, std::set<size_t> >::const_iterator itks = rowInfo.tmpEvals.find(j);
            if (itks != rowInfo.tmpEvals.end()) {
                const std::set<size_t>& ks = itks->second;
                std::set<size_t>::const_iterator itk;
                for (itk = ks.begin(); itk != ks.end(); ++itk) {
                    size_t k = *itk;
                    size_t tapeJ = lModel.getTempIndepIndexes(k)->tape;

                    jacVal += dyiDxtape[tapeI].at(tapeJ) * dzDx[k].at(j);
                }
            }

            jacVal *= py;

            indexedLoopResults[jacLE++] = createJacobianElement(handler, positions, nnz,
                                                                jacVal, iterationIndexOp, ifElses,
                                                                allLocations);
        }
    }


    namespace loops {

        template<class Base>
        std::pair<CG<Base>, IndexPattern*> createJacobianElement(CodeHandler<Base>& handler,
                                                                 const std::vector<size_t>& positions,
                                                                 size_t maxPos,
                                                                 const CG<Base>& dfdx,
                                                                 IndexOperationNode<Base>& iterationIndexOp,
                                                                 vector<IfElseInfo<Base> >& ifElses,
                                                                 std::set<size_t>& allLocations) {
            using namespace std;

            size_t nIter = positions.size();

            map<size_t, size_t> locations;
            for (size_t iter = 0; iter < nIter; iter++) {
                if (positions[iter] != maxPos) {
                    locations[iter] = positions[iter];
                    allLocations.insert(positions[iter]);
                }
            }

            IndexPattern* pattern;
            if (locations.size() == nIter) {
                // present in all iterations

                // generate the index pattern for the jacobian compressed element
                pattern = IndexPattern::detect(positions);
                handler.manageLoopDependentIndexPattern(pattern);
            } else {
                /**
                 * must create a conditional element so that this 
                 * contribution to the jacobian is only evaluated at the
                 * relevant iterations
                 */

                // generate the index pattern for the jacobian compressed element
                pattern = IndexPattern::detect(locations);
                handler.manageLoopDependentIndexPattern(pattern);
            }


            return createLoopResult(handler, locations, nIter,
                                    dfdx, pattern, 1,
                                    iterationIndexOp, ifElses);

        }

    }

}

#endif