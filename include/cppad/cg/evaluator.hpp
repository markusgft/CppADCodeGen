#ifndef CPPAD_CG_EVALUATOR_INCLUDED
#define CPPAD_CG_EVALUATOR_INCLUDED
/* --------------------------------------------------------------------------
 *  CppADCodeGen: C++ Algorithmic Differentiation with Source Code Generation:
 *    Copyright (C) 2012 Ciengis
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
namespace cg {

/**
 * Utility class used for some code transformation
 */
template<class Base, class BaseOut>
class Evaluator {
protected:
    const CodeHandler<Base>& handler_;
    const std::vector<CG<Base> > dep_;
    const std::vector<CppAD::AD<BaseOut > >* indep_;
    std::map<OperationNode<Base>*, CppAD::AD<BaseOut> > evals_;
    std::map<OperationNode<Base>*, CppAD::vector<CppAD::AD<BaseOut> >* > evalsArrays_;
    std::set<OperationNode<Base>*> evalsAtomic_;
    std::map<size_t, atomic_base<BaseOut>* > atomicFunctions_;
public:

    /**
     * @param handler The source code handler
     * @param dep Dependent variable vector (all variables must belong to
     *             the same code handler)
     */
    Evaluator(const CodeHandler<Base>& handler, const std::vector<CG<Base> >& dep) :
        handler_(handler),
        dep_(dep),
        indep_(nullptr) {
    }

    /**
     * Provides an atomic function.
     * 
     * @param id The atomic function ID
     * @param atomic The atomic function
     * @return True if an atomic function with the same ID was already
     *         defined, false otherwise.
     */
    virtual bool addAtomicFunction(size_t id, atomic_base<BaseOut>& atomic) {
        bool exists = atomicFunctions_.find(id) != atomicFunctions_.end();
        atomicFunctions_[id] = &atomic;
        return exists;
    }

    virtual void addAtomicFunctions(const std::map<size_t, atomic_base<BaseOut>* >& atomics) {
        for (const auto& it : atomics) {
            atomic_base<BaseOut>* atomic = it.second;
            if (atomic != nullptr) {
                atomicFunctions_[it.first] = atomic;
            }
        }
    }

    /**
     * Performs all the operations required to calculate the dependent 
     * variables with a (potentially) new data type
     * 
     * @param indep The new independent variables.
     * @return The dependent variable values
     */
    inline std::vector<CppAD::AD<BaseOut> > evaluate(const std::vector<CppAD::AD<BaseOut> >& indep) throw (CGException) {
        if (handler_.getIndependentVariableSize() != indep.size()) {
            std::stringstream ss;
            ss << "Invalid independent variable size. Expected " << handler_.getIndependentVariableSize() << " but got " << indep.size() << ".";
            throw CGException(ss.str());
        }

        indep_ = &indep;

        clear(); // clean-up

        std::vector<CppAD::AD<BaseOut> > newDep(dep_.size());

        for (size_t i = 0; i < dep_.size(); i++) {
            newDep[i] = evalCG(dep_[i]);
        }

        clear(); // clean-up

        return newDep;
    }

    inline virtual ~Evaluator() {
        clear();
    }

private:

    /**
     * clean-up
     */
    inline void clear() {
        evals_.clear();

        for (const auto& it : evalsArrays_) {
            delete it.second;
        }
        evalsArrays_.clear();
    }

    inline CppAD::AD<BaseOut> evalCG(const CG<Base>& dep) throw (CGException) {
        if (dep.isParameter()) {
            // parameter
            return CppAD::AD<BaseOut> (dep.getValue());
        } else {
            return evalOperations(*dep.getOperationNode());
        }
    }

    inline CppAD::AD<BaseOut> evalArg(const Argument<Base>& arg) throw (CGException) {
        if (arg.getOperation() != nullptr) {
            return evalOperations(*arg.getOperation());
        } else {
            // parameter
            return AD<BaseOut> (*arg.getParameter());
        }
    }

    inline CppAD::AD<BaseOut> evalOperations(OperationNode<Base>& node) throw (CGException) {
        using CppAD::vector;

        // check if this node was previously determined
        const auto it = evals_.find(&node);
        if (it != evals_.end()) {
            return it->second;
        }

        // first evaluation of this node
        const std::vector<Argument<Base> >& args = node.getArguments();
        const CGOpCode code = node.getOperationType();
        AD<BaseOut> result;
        switch (code) {
            case CGOpCode::Assign:
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for assign()");
                result = evalArg(args[0]);
                break;
            case CGOpCode::Abs: //  abs(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for abs()");
                result = abs(evalArg(args[0]));
                break;
            case CGOpCode::Acos: // acos(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for acos()");
                result = acos(evalArg(args[0]));
                break;
            case CGOpCode::Add: //  a + b
                CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for addition");
                result = evalArg(args[0]) + evalArg(args[1]);
                break;
            case CGOpCode::Alias:
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for alias");
                result = evalArg(args[0]);
                break;
                //case CGArrayCreationOp: // {a, b, c ...}
            case CGOpCode::ArrayElement: // x[i]
            {
                const std::vector<size_t>& info = node.getInfo();
                CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for array element");
                CPPADCG_ASSERT_KNOWN(args[0].getOperation() != nullptr, "Invalid argument for array element");
                CPPADCG_ASSERT_KNOWN(args[1].getOperation() != nullptr, "Invalid argument for array element");
                CPPADCG_ASSERT_KNOWN(info.size() == 1, "Invalid number of information data for array element");
                size_t index = info[0];
                vector<AD<BaseOut> >& array = evalArrayCreationOperation(*args[0].getOperation()); // array creation
                evalAtomicOperation(*args[1].getOperation()); // atomic operation

                result = array[index];
                break;
            }
            case CGOpCode::Asin: // asin(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for asin()");
                result = asin(evalArg(args[0]));
                break;
            case CGOpCode::Atan: // atan(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for atan()");
                result = atan(evalArg(args[0]));
                break;
                //CGAtomicForwardOp
                //CGAtomicReverseOp
            case CGOpCode::ComLt: // result = left < right? trueCase: falseCase
                CPPADCG_ASSERT_KNOWN(args.size() == 4, "Invalid number of arguments for CondExpOp(CompareLt, )");
                result = CondExpOp(CompareLt, evalArg(args[0]), evalArg(args[1]), evalArg(args[2]), evalArg(args[3]));
                break;
            case CGOpCode::ComLe: // result = left <= right? trueCase: falseCase
                CPPADCG_ASSERT_KNOWN(args.size() == 4, "Invalid number of arguments for CondExpOp(CompareLe, )");
            {
                AD<BaseOut> a0 = evalArg(args[0]);
                AD<BaseOut> a1 = evalArg(args[1]);
                AD<BaseOut> a2 = evalArg(args[2]);
                AD<BaseOut> a3 = evalArg(args[3]);
                result = CondExpOp(CompareLe, a0, a1, a2, a3);
            }
                break;
            case CGOpCode::ComEq: // result = left == right? trueCase: falseCase
                CPPADCG_ASSERT_KNOWN(args.size() == 4, "Invalid number of arguments for CondExpOp(CompareEq, )");
                result = CondExpOp(CompareEq, evalArg(args[0]), evalArg(args[1]), evalArg(args[2]), evalArg(args[3]));
                break;
            case CGOpCode::ComGe: // result = left >= right? trueCase: falseCase
                CPPADCG_ASSERT_KNOWN(args.size() == 4, "Invalid number of arguments for CondExpOp(CompareGe, )");
                result = CondExpOp(CompareGe, evalArg(args[0]), evalArg(args[1]), evalArg(args[2]), evalArg(args[3]));
                break;
            case CGOpCode::ComGt: // result = left > right? trueCase: falseCase
                CPPADCG_ASSERT_KNOWN(args.size() == 4, "Invalid number of arguments for CondExpOp(CompareGt, )");
                result = CondExpOp(CompareGt, evalArg(args[0]), evalArg(args[1]), evalArg(args[2]), evalArg(args[3]));
                break;
            case CGOpCode::ComNe: // result = left != right? trueCase: falseCase
                CPPADCG_ASSERT_KNOWN(args.size() == 4, "Invalid number of arguments for CondExpOp(CompareNe, )");
                result = CondExpOp(CompareNe, evalArg(args[0]), evalArg(args[1]), evalArg(args[2]), evalArg(args[3]));
                break;
            case CGOpCode::Cosh: // cosh(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for cosh()");
                result = cosh(evalArg(args[0]));
                break;
            case CGOpCode::Cos: //  cos(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for cos()");
                result = cos(evalArg(args[0]));
                break;
            case CGOpCode::Div: // a / b
                CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for division");
                result = evalArg(args[0]) / evalArg(args[1]);
                break;
            case CGOpCode::Exp: //  exp(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for exp()");
                result = exp(evalArg(args[0]));
                break;
            case CGOpCode::Inv: //                             independent variable
            {
                size_t index = handler_.getIndependentVariableIndex(node);
                result = (*indep_)[index];
            }
                break;
            case CGOpCode::Log: //  log(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for log()");
                result = log(evalArg(args[0]));
                break;
            case CGOpCode::Mul: // a * b
                CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for multiplication");
                result = evalArg(args[0]) * evalArg(args[1]);
                break;
            case CGOpCode::Pow: //  pow(a,   b)
                CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for pow()");
                result = pow(evalArg(args[0]), evalArg(args[1]));
                break;
                //case PriOp: //  PrintFor(text, parameter or variable, parameter or variable)
            case CGOpCode::Sign: // result = (x > 0)? 1.0:((x == 0)? 0.0:-1)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for sign()");
                result = sign(evalArg(args[0]));
                break;
            case CGOpCode::Sinh: // sinh(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for sinh()");
                result = sinh(evalArg(args[0]));
                break;
            case CGOpCode::Sin: //  sin(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for sin()");
                result = sin(evalArg(args[0]));
                break;
            case CGOpCode::Sqrt: // sqrt(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for sqrt()");
                result = sqrt(evalArg(args[0]));
                break;
            case CGOpCode::Sub: //  a - b
                CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for subtraction");
                result = evalArg(args[0]) - evalArg(args[1]);
                break;
            case CGOpCode::Tanh: //  tanh(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for tanh()");
                result = tanh(evalArg(args[0]));
                break;
            case CGOpCode::Tan: //  tan(variable)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for tan()");
                result = tan(evalArg(args[0]));
                break;
            case CGOpCode::UnMinus: // -(a)
                CPPADCG_ASSERT_KNOWN(args.size() == 1, "Invalid number of arguments for unary minus");
                result = -evalArg(args[0]);
                break;
            default:
            {
                std::stringstream ss;
                ss << "Unknown operation code '" << code << "'";
                throw CGException(ss.str());
            }
        }

        evals_[&node] = result;

        return result;
    }

    inline CppAD::vector<CppAD::AD<BaseOut> >& evalArrayCreationOperation(OperationNode<Base>& node) throw (CGException) {
        using CppAD::vector;

        CPPADCG_ASSERT_KNOWN(node.getOperationType() == CGOpCode::ArrayCreation, "Invalid array creation operation");

        const auto it = evalsArrays_.find(&node);
        if (it != evalsArrays_.end()) {
            return *it->second;
        }

        const std::vector<Argument<Base> >& args = node.getArguments();
        vector<AD<BaseOut> >* resultArray = new vector<AD<BaseOut> >(args.size());
        evalsArrays_[&node] = resultArray;

        for (size_t a = 0; a < args.size(); a++) {
            (*resultArray)[a] = evalArg(args[a]);
        }

        return *resultArray;
    }

    inline void evalAtomicOperation(OperationNode<Base>& node) throw (CGException) {
        using CppAD::vector;

        if (evalsAtomic_.find(&node) != evalsAtomic_.end()) {
            return;
        }

        if (node.getOperationType() != CGOpCode::AtomicForward) {
            throw CGException("Evaluator can only handle zero forward mode for atomic functions");
        }

        const std::vector<size_t>& info = node.getInfo();
        const std::vector<Argument<Base> >& args = node.getArguments();
        CPPADCG_ASSERT_KNOWN(args.size() == 2, "Invalid number of arguments for atomic forward mode");
        CPPADCG_ASSERT_KNOWN(info.size() == 3, "Invalid number of information data for atomic forward mode");

        // find the atomic function
        size_t id = info[0];
        typename std::map<size_t, atomic_base<BaseOut>* >::const_iterator itaf = atomicFunctions_.find(id);
        atomic_base<BaseOut>* atomicFunction = nullptr;
        if (itaf != atomicFunctions_.end()) {
            atomicFunction = itaf->second;
        }

        if (atomicFunction == nullptr) {
            std::stringstream ss;
            ss << "No atomic function defined in the evaluator for ";
            const std::string* atomName = handler_.getAtomicFunctionName(id);
            if (atomName != nullptr) {
                ss << "'" << *atomName << "'";
            } else
                ss << "id '" << id << "'";
            throw CGException(ss.str());
        }

        size_t p = info[2];
        if (p != 0) {
            throw CGException("Evaluator can only handle zero forward mode for atomic functions");
        }
        const vector<AD<BaseOut> >& ax = evalArrayCreationOperation(*args[0].getOperation());
        vector<AD<BaseOut> >& ay = evalArrayCreationOperation(*args[1].getOperation());

        (*atomicFunction)(ax, ay);

        evalsAtomic_.insert(&node);
    }

};

} // END cg namespace
} // END CppAD namespace

#endif