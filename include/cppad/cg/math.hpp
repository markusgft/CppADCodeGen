#ifndef CPPAD_CG_MATH_INCLUDED
#define CPPAD_CG_MATH_INCLUDED
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

#define CPPAD_CG_CREATE_OPERATION(OpName, OpCode)                              \
    template<class Base>                                                       \
    inline cg::CG<Base> OpName(const cg::CG<Base>& var) {                      \
        using namespace CppAD::cg;                                             \
        if (var.isParameter()) {                                               \
            return CG<Base> (OpName(var.getValue()));                          \
        } else {                                                               \
            CG<Base> result(*var.getCodeHandler(), new OperationNode<Base>(CppAD::cg::CGOpCode::OpCode, var.argument()));\
            if(var.isValueDefined())                                           \
                result.setValue(OpName(var.getValue()));                       \
            return result;                                                     \
        }                                                                      \
    }

CPPAD_CG_CREATE_OPERATION(abs, Abs)
CPPAD_CG_CREATE_OPERATION(acos, Acos)
CPPAD_CG_CREATE_OPERATION(asin, Asin)
CPPAD_CG_CREATE_OPERATION(atan, Atan)
CPPAD_CG_CREATE_OPERATION(cos, Cos)
CPPAD_CG_CREATE_OPERATION(cosh, Cosh)
CPPAD_CG_CREATE_OPERATION(exp, Exp)
CPPAD_CG_CREATE_OPERATION(log, Log)
CPPAD_CG_CREATE_OPERATION(sin, Sin)
CPPAD_CG_CREATE_OPERATION(sinh, Sinh)
CPPAD_CG_CREATE_OPERATION(sqrt, Sqrt)
CPPAD_CG_CREATE_OPERATION(tan, Tan)
CPPAD_CG_CREATE_OPERATION(tanh, Tanh)

} // END CppAD namespace

#endif