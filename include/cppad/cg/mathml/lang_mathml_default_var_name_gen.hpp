#ifndef CPPAD_CG_LANG_MATHML_DEFAULT_VAR_NAME_GEN_INCLUDED
#define CPPAD_CG_LANG_MATHML_DEFAULT_VAR_NAME_GEN_INCLUDED
/* --------------------------------------------------------------------------
 *  CppADCodeGen: C++ Algorithmic Differentiation with Source Code Generation:
 *    Copyright (C) 2015 Ciengis
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
 * Creates variables names for MathML source code.
 * 
 * @author Joao Leal
 */
template<class Base>
class LangMathMLDefaultVariableNameGenerator : public VariableNameGenerator<Base> {
protected:
    // auxiliary string stream
    std::stringstream _ss;
    // array name of the dependent variables
    std::string _depName;
    // array name of the independent variables
    std::string _indepName;
    // array name of the temporary variables
    std::string _tmpName;
    // array name of the temporary array variables
    std::string _tmpArrayName;
    // sparse array name of the temporary array variables
    std::string _tmpSparseArrayName;
    // the lowest variable ID used for the temporary variables
    size_t _minTemporaryID;
    // the highest variable ID used for the temporary variables
    size_t _maxTemporaryID;
    // the highest ID used for the temporary array variables
    size_t _maxTemporaryArrayID;
    // the highest ID used for the temporary sparse array variables
    size_t _maxTemporarySparseArrayID;
public:

    inline LangMathMLDefaultVariableNameGenerator(const std::string& depName = "y",
                                                  const std::string& indepName = "x",
                                                  const std::string& tmpName = "v",
                                                  const std::string& tmpArrayName = "a",
                                                  const std::string& tmpSparseArrayName = "s") :
        _depName(depName),
        _indepName(indepName),
        _tmpName(tmpName),
        _tmpArrayName(tmpArrayName),
        _tmpSparseArrayName(tmpSparseArrayName) {
        this->_independent.push_back(FuncArgument(_indepName));
        this->_dependent.push_back(FuncArgument(_depName));
        this->_temporary.push_back(FuncArgument(_tmpName));
        this->_temporary.push_back(FuncArgument(_tmpArrayName));
        this->_temporary.push_back(FuncArgument(_tmpSparseArrayName));
    }

    inline virtual size_t getMinTemporaryVariableID() const override {
        return _minTemporaryID;
    }

    inline virtual size_t getMaxTemporaryVariableID() const override {
        return _maxTemporaryID;
    }

    inline virtual size_t getMaxTemporaryArrayVariableID() const override {
        return _maxTemporaryArrayID;
    }

    virtual size_t getMaxTemporarySparseArrayVariableID() const override {
        return _maxTemporarySparseArrayID;
    }

    inline virtual std::string generateDependent(size_t index) override {
        _ss.clear();
        _ss.str("");

        _ss << "<msub>"
                "<mi>" << _depName << "</mi>"
                "<mn>" << index << "</mn>"
                "</msub>";

        return _ss.str();
    }

    inline virtual std::string generateIndependent(const OperationNode<Base>& independent) override {
        _ss.clear();
        _ss.str("");

        size_t id = independent.getVariableID();

        _ss << "<msub>"
                "<mi>" << _indepName << "</mi>"
                "<mn>" << (id - 1) << "</mn>"
                "</msub>";

        return _ss.str();
    }

    inline virtual std::string generateTemporary(const OperationNode<Base>& variable) override {
        _ss.clear();
        _ss.str("");

        size_t id = variable.getVariableID();
        if (this->_temporary[0].array) {
            _ss << "<msub>"
                    "<mi>" << _tmpName << "</mi>"
                    "<mn>" << (id - this->_minTemporaryID) << "</mn>"
                    "</msub>";
        } else {
            _ss << "<msub>"
                    "<mi>" << _tmpName << "</mi>"
                    "<mn>" << id << "</mn>"
                    "</msub>";
        }

        return _ss.str();
    }

    virtual std::string generateTemporaryArray(const OperationNode<Base>& variable) override {
        _ss.clear();
        _ss.str("");

        CPPADCG_ASSERT_UNKNOWN(variable.getOperationType() == CGOpCode::ArrayCreation);

        size_t id = variable.getVariableID();
        _ss << "<mi>" << _tmpArrayName << "</mi>"
                "<mfenced open='[' close=']'>"
                "<mrow>"
                "<mn>" << id - 1 << "</mn>"
                "<mo>:</mo>"///////////////////////////////////////// TODO
                "</mrow>"
                "</mfenced>";

        return _ss.str();
    }

    virtual std::string generateTemporarySparseArray(const OperationNode<Base>& variable) override {
        _ss.clear();
        _ss.str("");

        CPPADCG_ASSERT_UNKNOWN(variable.getOperationType() == CGOpCode::SparseArrayCreation);

        size_t id = variable.getVariableID();
        _ss << "<mi>" << _tmpSparseArrayName << "</mi>"
                "<mfenced open='[' close=']'>"
                "<mrow>"
                "<mn>" << id - 1 << "</mn>"
                "<mo>:</mo>"///////////////////////////////////////// TODO
                "</mrow>"
                "</mfenced>";

        return _ss.str();
    }

    virtual std::string generateIndexedDependent(const OperationNode<Base>& var,
                                                 const IndexPattern& ip) override {
        CPPADCG_ASSERT_KNOWN(var.getOperationType() == CGOpCode::LoopIndexedDep, "Invalid node type");
        CPPADCG_ASSERT_KNOWN(!var.getArguments().empty(), "Invalid number of arguments");

        _ss.clear();
        _ss.str("");

        _ss << "<msub>"
                "<mi>" << _depName << "</mi>";
        LanguageMathML<Base>::indexPattern2String(_ss, ip, getIndexes(var, 1));
        _ss << "</msub>";

        return _ss.str();
    }

    virtual std::string generateIndexedIndependent(const OperationNode<Base>& independent,
                                                   const IndexPattern& ip) override {
        CPPADCG_ASSERT_KNOWN(independent.getOperationType() == CGOpCode::LoopIndexedIndep, "Invalid node type");
        CPPADCG_ASSERT_KNOWN(independent.getArguments().size() > 0, "Invalid number of arguments");

        _ss.clear();
        _ss.str("");


        _ss << "<msub>"
                "<mi>" << _indepName << "</mi>";
        LanguageMathML<Base>::indexPattern2String(_ss, ip, getIndexes(independent));
        _ss << "</msub>";

        return _ss.str();
    }

    inline virtual void setTemporaryVariableID(size_t minTempID,
                                               size_t maxTempID,
                                               size_t maxTempArrayID,
                                               size_t maxTempSparseArrayID) override {
        _minTemporaryID = minTempID;
        _maxTemporaryID = maxTempID;
        _maxTemporaryArrayID = maxTempArrayID;
        _maxTemporarySparseArrayID = maxTempSparseArrayID;

        // if
        //  _minTemporaryID == _maxTemporaryID + 1
        // then no temporary variables are being used
        CPPADCG_ASSERT_UNKNOWN(_minTemporaryID <= _maxTemporaryID + 1);
    }

    virtual const std::string& getIndependentArrayName(const OperationNode<Base>& indep) override {
        return _indepName;
    }

    virtual size_t getIndependentArrayIndex(const OperationNode<Base>& indep) override {
        return indep.getVariableID() - 1;
    }

    virtual bool isConsecutiveInIndepArray(const OperationNode<Base>& indepFirst,
                                           const OperationNode<Base>& indepSecond) override {
        return indepFirst.getVariableID() + 1 == indepSecond.getVariableID();
    }

    virtual bool isInSameIndependentArray(const OperationNode<Base>& indep1,
                                          const OperationNode<Base>& indep2) override {
        return true;
    }

    virtual const std::string& getTemporaryVarArrayName(const OperationNode<Base>& var) override {
        return _tmpName;
    }

    virtual size_t getTemporaryVarArrayIndex(const OperationNode<Base>& var) override {
        size_t id = var.getVariableID();
        return id - this->_minTemporaryID;
    }

    virtual bool isConsecutiveInTemporaryVarArray(const OperationNode<Base>& varFirst,
                                                  const OperationNode<Base>& varSecond) override {
        return varFirst.getVariableID() + 1 == varSecond.getVariableID();
    }

    virtual bool isInSameTemporaryVarArray(const OperationNode<Base>& var1,
                                           const OperationNode<Base>& var2) override {
        return true;
    }

    inline virtual ~LangMathMLDefaultVariableNameGenerator() {
    }
protected:

    static inline std::vector<const IndexDclrOperationNode<Base>*> getIndexes(const OperationNode<Base>& var, size_t offset = 0) {
        const std::vector<Argument<Base> >& args = var.getArguments();
        std::vector<const IndexDclrOperationNode<Base>*> indexes(args.size() - offset);

        for (size_t a = offset; a < args.size(); a++) {
            CPPADCG_ASSERT_KNOWN(args[a].getOperation() != nullptr, "Invalid argument");
            CPPADCG_ASSERT_KNOWN(args[a].getOperation()->getOperationType() == CGOpCode::Index, "Invalid argument");

            indexes[a - offset] = &static_cast<const IndexOperationNode<Base>*> (args[a].getOperation())->getIndex();
        }

        return indexes;
    }
};

} // END cg namespace
} // END CppAD namespace

#endif