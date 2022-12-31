#include "CompilationEngine.h"
#include "definitions.h"
#include <magic_enum.hpp>
#include <string>

auto CompilationEngine::tokenTypeToString(TokenType token) -> std::string
{
    std::string type = "";
    if (mTokenizer->tokenType() == TokenType::KEYWORD)
        type = std::string{"keyword"};
    else if (mTokenizer->tokenType() == TokenType::SYMBOL)
        type = std::string{"symbol"};
    else if (mTokenizer->tokenType() == TokenType::IDENTIFIER)
        type = std::string{"identifier"};
    else if (mTokenizer->tokenType() == TokenType::INT_CONST)
        type = std::string{"integerConstant"};
    else if (mTokenizer->tokenType() == TokenType::STRING_CONST)
        type = std::string{"stringConstant"};

    return type;
}

auto CompilationEngine::write(TokenType type, std::string data) -> void
{
    auto type_str = this->tokenTypeToString(type);

    for (int i = 0; i < mDepth; i++)
        mOutputFile << "  ";

    mOutputFile << "<";
    mOutputFile << type_str;
    mOutputFile << "> ";

    mOutputFile << data;

    // Special case handling for identifier.
    if (type == TokenType::IDENTIFIER)
        this->handleIdentifier(data);

    mOutputFile << " </";
    mOutputFile << type_str;
    mOutputFile << ">\n";
};

auto CompilationEngine::handleIdentifier(std::string name) -> void
{
    // Determine kind.
    auto kind = mTokenizer->lastKind();

    // Determine type.
    auto type = mTokenizer->prevToken();

    // Edge-casing, to avoid including int, Array etc. Needs static and field and stuff
    // too.
    if (type == std::string{"var"})
        return;

    if (type == std::string{","})
        type = mPrevType;

    // Insert into table.
    if (kind != Kind::NONE && std::all_of(name.begin(), name.end(), &::islower) &&
        type != ".")
        mSymbolTable->define(name, type, kind);
    else if (kind == Kind::ARG)
        mSymbolTable->define(name, type, kind);

    // Get the correct index.
    auto index = mSymbolTable->indexOf(name);

    // Write all data.
    mOutputFile << ";type:" << type << ";kind:";
    mOutputFile << magic_enum::enum_name(kind) << ";index:";
    mOutputFile << std::to_string(index) << ";";

    // Remember type
    mPrevType = type;
};

auto CompilationEngine::write(std::string data) -> void
{
    for (int i = 0; i < mDepth; i++)
        mOutputFile << "  ";
    mOutputFile << data;
    mOutputFile << "\n";
}

auto CompilationEngine::compileClass() -> void
{
    // class token
    this->write(std::string{"<class>"});
    mDepth++;

    // class Main {
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mClassName = mTokenizer->token();
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    int nFields = 0;
    while (mTokenizer->token() != std::string{"}"})
    {
        if (mTokenizer->token() == "field" || mTokenizer->token() == "static")
            nFields += this->compileClassVarDecl();
        else if (mTokenizer->token() == "constructor")
            this->compileSubroutine(nFields, FunctionType::CONSTRUCTOR);
        else if (mTokenizer->token() == "function")
            this->compileSubroutine(nFields, FunctionType::FUNCTION);
        else if (mTokenizer->token() == "method")
            this->compileSubroutine(nFields, FunctionType::METHOD);

        // Get the next token.
        mTokenizer->advance();
    }

    // }
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mDepth--;

    // class
    this->write(std::string{"</class>"});
};

auto CompilationEngine::compileClassVarDecl() -> int
{
    // classVarDec
    this->write(std::string{"<classVarDec>"});
    mDepth++;

    int nFields = 0;
    while (mTokenizer->token() != std::string{";"})
    {
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        // Need to know number of attributes for the class.
        if (mSymbolTable->indexOf(mTokenizer->token()) > -1)
            nFields++;
        mTokenizer->advance();
    }

    //;
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // classVarDec
    mDepth--;
    this->write(std::string{"</classVarDec>"});

    return nFields;
}

auto CompilationEngine::compileSubroutine(int nFields, FunctionType functionType) -> void
{
    // subroutineDec
    this->write(std::string{"<subroutineDec>"});
    mDepth++;
    mSymbolTable->startSubroutine();

    // function void main (
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();
    mTokenizer->setKind(Kind::NONE); // Hacky way to prevent constr. entry in symboltable
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    std::string funcName = mTokenizer->token();
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // Write parameterlist, there can only be one.
    this->compileParameterList();

    // )
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // call subroutinebody
    this->compileSubroutineBody(funcName, nFields, functionType);

    // subroutineDec
    mDepth--;
    this->write(std::string{"</subroutineDec>"});
}

auto CompilationEngine::compileSubroutineBody(std::string name, int nFields,
                                              FunctionType functionType) -> void
{
    // subroutineBody
    this->write(std::string{"<subroutineBody>"});
    mDepth++;

    // {
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    int nLocals = 0;
    while (mTokenizer->token() != std::string{"}"})
    {
        mTokenizer->advance();
        if (mTokenizer->token() == std::string{"var"})
        {
            nLocals += this->compileVarDecl();
        }
        else
        {
            // function main.Main {nLocals}
            mVMWriter->writeFunction(mClassName, name, nLocals);
            // Special case constructor.
            if (functionType == FunctionType::CONSTRUCTOR)
            {
                // Allocate memory for the class and set pointer.
                mVMWriter->writePush(Segment::CONST, nFields);
                mVMWriter->writeCall("Memory.alloc", 1);
                mVMWriter->writePop(Segment::POINTER, 0);
            }
            // First argument is implicit this.
            else if (functionType == FunctionType::METHOD)
            {
                mVMWriter->writePush(Segment::ARG, 0);
                mVMWriter->writePop(Segment::POINTER, 0);
            }
            // Else just do nothing at all
            else
            {
            }
            this->compileStatements();
        }
    }

    // }
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // subroutineBody
    mDepth--;
    this->write(std::string{"</subroutineBody>"});
}

auto CompilationEngine::compileVarDecl() -> int
{
    this->write(std::string{"<varDec>"});
    mDepth++;
    int mVars = 0;
    while (mTokenizer->token() != std::string{";"})
    {
        if ((mTokenizer->tokenType() == TokenType::IDENTIFIER) &&
            (std::islower(mTokenizer->token()[0])))
            mVars++;
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        mTokenizer->advance();
    }
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mDepth--;
    this->write(std::string{"</varDec>"});
    return mVars;
};

auto CompilationEngine::compileStatements() -> void
{
    this->write(std::string{"<statements>"});
    mDepth++;

    while (mTokenizer->token() != std::string{"}"})
    {
        if (mTokenizer->token() == std::string{"let"})
        {
            this->compileLet();
            mTokenizer->advance();
        }
        else if (mTokenizer->token() == std::string{"do"})
        {
            this->compileDo();
            mTokenizer->advance();
        }
        else if (mTokenizer->token() == std::string{"while"})
        {
            this->compileWhile();
            mTokenizer->advance();
        }
        else if (mTokenizer->token() == std::string{"return"})
        {
            this->compileReturn();
            mTokenizer->advance();
        }
        else if (mTokenizer->token() == std::string{"if"})
        {
            this->compileIf();
        }
        else
            mTokenizer->advance();
    }

    mDepth--;
    this->write(std::string{"</statements>"});
}

auto CompilationEngine::compileLet() -> void
{
    this->write(std::string{"<letStatement>"});
    mDepth++;

    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();

    std::string varName;
    while (mTokenizer->token() != std::string{";"})
    {
        this->write(mTokenizer->tokenType(), mTokenizer->token());

        if (mTokenizer->token() == std::string{"="} ||
            mTokenizer->token() == std::string{"["})
        {
            varName = mTokenizer->prevToken();
            compileExpression(true);
        }
        else
        {
            mTokenizer->advance();
        }
    }
    // ;
    auto kind = mSymbolTable->kindOf(varName);
    auto varIndex = mSymbolTable->indexOf(varName);
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mVMWriter->writePop(kindToSegment[kind], varIndex);
    mDepth--;
    this->write(std::string{"</letStatement>"});
}

auto CompilationEngine::compileExpression(bool isLet) -> int
{
    mTokenizer->advance();
    if (mTokenizer->token() == std::string{")"} ||
        mTokenizer->token() == std::string{";"})
        return 0;
    this->write(std::string{"<expression>"});
    mDepth++;

    std::string curOperator = "";
    while (mTokenizer->token() != std::string{";"} &&
           mTokenizer->token() != std::string{"]"} &&
           mTokenizer->token() != std::string{")"})
    {
        this->compileTerm(isLet);
        if (mTokenizer->token() == std::string{","})
            break;
        if (std::find(ops.begin(), ops.end(), mTokenizer->token()) != ops.end())
        {
            curOperator = mTokenizer->token();
            this->write(mTokenizer->tokenType(), mTokenizer->token());
            mTokenizer->advance();
        }
    }
    if (curOperator != std::string{""})
        mVMWriter->writeArithmetic(curOperator);
    mDepth--;
    this->write(std::string{"</expression>"});
    return 1;
}

auto CompilationEngine::compileTerm(bool isLet) -> void
{
    this->write(std::string{"<term>"});
    mDepth++;
    int i = 0;
    int nArgs = 0;
    bool isOp = false;
    bool isNegNotOp = false;
    bool isFuncCall = false;
    std::string className;
    std::string funcName;
    std::string opName = "";
    while (mTokenizer->token() != ")" && mTokenizer->token() != ";" &&
           mTokenizer->token() != "]" &&
           (std::find(ops.begin(), ops.end(), mTokenizer->token()) == ops.end() ||
            (i == 0)))
    {
        // Push integer constants on stack.
        if (mTokenizer->tokenType() == TokenType::INT_CONST)
            mVMWriter->writePush(Segment::CONST, std::stoi(mTokenizer->token()));
        if (mTokenizer->tokenType() == TokenType::IDENTIFIER &&
            mSymbolTable->indexOf(mTokenizer->token()) != -1)
        {
            auto index = mSymbolTable->indexOf(mTokenizer->token());
            auto kind = mSymbolTable->kindOf(mTokenizer->token());
            mVMWriter->writePush(kindToSegment[kind], index);
        }
        if (mTokenizer->token() == "true" || mTokenizer->token() == "false")
        {
            mVMWriter->writePush(Segment::CONST, 0);
            if (mTokenizer->token() == "true")
                mVMWriter->writeArithmetic("~");
        }
        if (mTokenizer->token() == "this")
        {
            mVMWriter->writePush(Segment::POINTER, 0);
        }

        this->write(mTokenizer->tokenType(), mTokenizer->token());
        if (mTokenizer->token() == "(")
        {
            funcName = mTokenizer->prevToken();
            if (mTokenizer->prevTokenType() == TokenType::IDENTIFIER)
                nArgs = this->compileExpressionList(isLet);
            else
                this->compileExpression(isLet);
            this->write(mTokenizer->tokenType(), mTokenizer->token());
            if (isFuncCall)
            {
                // TODO: This hack should not be necessary. Missing key insight...
                if (isLet && (nArgs == 1))
                    nArgs--;
                mVMWriter->writeCall(className + std::string{"."} + funcName, nArgs);
                isFuncCall = false;
            }
        }
        if (mTokenizer->token() == "[")
        {
            this->compileExpression(isLet);
            this->write(mTokenizer->tokenType(), mTokenizer->token());
            mTokenizer->advance();
            break;
        }
        if (std::find(ops.begin(), ops.end(), mTokenizer->token()) != ops.end())
        {
            isOp = true;
            if (mTokenizer->prevTokenType() == TokenType::SYMBOL &&
                mTokenizer->token() != "~")
                isNegNotOp = true;
            else
                isNegNotOp = false;
        }
        if (mTokenizer->token() == ".")
        {
            isFuncCall = true;
            auto token = mTokenizer->prevToken();
            // Class names must start with caps.
            if (std::isupper(token[0]))
                className = token;
            else
                className = mSymbolTable->typeOf(token);
        }
        // )
        auto opName = mTokenizer->token();
        mTokenizer->advance();

        if (mTokenizer->tokenType() == TokenType::INT_CONST ||
            mTokenizer->tokenType() == TokenType::STRING_CONST ||
            (mTokenizer->tokenType() == TokenType::IDENTIFIER && (i == 0)) ||
            mTokenizer->token() == "(" &&
                std::find(ops.begin(), ops.end(), mTokenizer->prevToken()) != ops.end())
        {
            this->compileTerm(isLet);
            if (isOp)
            {
                if (isNegNotOp)
                    mVMWriter->writeArithmetic(commandToString["neg"]);
                else
                    mVMWriter->writeArithmetic(commandToString[opName]);
                isOp = false;
            }
        }

        i++;
    }
    mDepth--;
    this->write(std::string{"</term>"});
}

auto CompilationEngine::compileExpressionList(bool isLet) -> int
{
    this->write(std::string{"<expressionList>"});
    mDepth++;
    int nArgs = 0;
    while (mTokenizer->token() != std::string{")"})
    {
        this->compileExpression(isLet);
        if (mTokenizer->token() == std::string{","})
            this->write(mTokenizer->tokenType(), mTokenizer->token());
        nArgs++;
    }
    mDepth--;
    this->write(std::string{"</expressionList>"});
    return nArgs;
}
auto CompilationEngine::compileDo() -> void
{
    this->write(std::string{"<doStatement>"});
    mDepth++;

    std::string callClass;
    std::string callMethod;
    int nArgs = 0;
    bool callClassIsVar = false;
    while (mTokenizer->token() != std::string{";"})
    {
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        if (mTokenizer->token() == std::string{"("})
        {
            callMethod = mTokenizer->prevToken();
            nArgs = this->compileExpressionList(false);
            this->write(mTokenizer->tokenType(), mTokenizer->token());
        }
        mTokenizer->advance();

        // Remember the class name for function call
        if (mTokenizer->prevToken() == std::string{"do"})
        {
            // Class names must start with caps.
            std::string token = mTokenizer->token();
            if (std::isupper(token[0]))
                callClass = token;
            else
            {
                // TODO: Note sure this is correct.
                callClass = mSymbolTable->typeOf(token);
                auto kind = mSymbolTable->kindOf(token);
                auto index = mSymbolTable->indexOf(token);

                // Handle the special case where the called method belongs to this.
                if (index == -1)
                {
                    callClass = mClassName;
                    mVMWriter->writePush(Segment::POINTER, 0);
                }
                else
                {
                    mVMWriter->writePush(kindToSegment[kind], index);
                }
            }
        }
    }

    // Write the function call.
    mVMWriter->writeCall(callClass + std::string{"."} + callMethod, nArgs);

    // Pop the implicit returned 0
    // TODO: Only for void functions!
    mVMWriter->writePop(Segment::TEMP, 0);

    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mDepth--;
    this->write(std::string{"</doStatement>"});
};
auto CompilationEngine::compileWhile() -> void
{
    this->write(std::string{"<whileStatement>"});
    mDepth++;
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();
    mVMWriter->writeLabel("WHILE_START" + std::to_string(mLabelCounter));
    int localLabelCounter = mLabelCounter;
    mLabelCounter++;
    while (mTokenizer->token() != std::string{"}"})
    {

        if (mTokenizer->token() == std::string{"("})
        {
            this->write(mTokenizer->tokenType(), mTokenizer->token());
            this->compileExpression(false);
            // Write explicit not, because expression must be not'ed in while.
            mVMWriter->writeArithmetic("not");
            this->write(mTokenizer->tokenType(), mTokenizer->token());
            mVMWriter->writeIf("WHILE_END" + std::to_string(localLabelCounter));
            mTokenizer->advance();
        }
        else if (mTokenizer->token() == std::string{"{"})
        {
            this->write(mTokenizer->tokenType(), mTokenizer->token());
            mTokenizer->advance();
            this->compileStatements();
        }
    }
    // goto L1
    mVMWriter->writeGoto("WHILE_START" + std::to_string(localLabelCounter));

    // label L2
    mVMWriter->writeLabel("WHILE_END" + std::to_string(localLabelCounter));

    // rest
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mDepth--;
    this->write(std::string{"</whileStatement>"});
};
auto CompilationEngine::compileReturn() -> void
{
    // returnStatement
    this->write(std::string{"<returnStatement>"});
    mDepth++;

    // return
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    int terms;
    while (mTokenizer->token() != std::string{";"})
    {
        terms = this->compileExpression(false);
    }

    // Push return 0 constant
    if (terms == 0)
        mVMWriter->writePush(Segment::CONST, 0);
    mVMWriter->writeReturn();

    // ;
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // returnStatement
    mDepth--;
    this->write(std::string{"</returnStatement>"});
};
auto CompilationEngine::compileIf() -> void
{
    int localLabelCounter = mLabelCounter;
    mLabelCounter++;
    // ifStatement
    this->write(std::string{"<ifStatement>"});
    mDepth++;

    // if
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();

    // (
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // cond
    this->compileExpression(false);

    // ~(cond)
    mVMWriter->writeArithmetic("not"); // ~(cond)

    // if-goto L1: L1 = IF_FALSE; L2 = IF_END
    mVMWriter->writeIf("IF_FALSE" + std::to_string(localLabelCounter));

    // ) {
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();
    this->write(mTokenizer->tokenType(), mTokenizer->token());

    // s1; }
    this->compileStatements();
    this->write(mTokenizer->tokenType(), mTokenizer->token());
    mTokenizer->advance();

    // goto label L2 = IF_END
    mVMWriter->writeGoto("IF_END" + std::to_string(localLabelCounter));

    // label L1
    mVMWriter->writeLabel("IF_FALSE" + std::to_string(localLabelCounter));

    // else
    if (mTokenizer->token() == std::string{"else"})
    {
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        mTokenizer->advance();
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        this->compileStatements();
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        mTokenizer->advance();
    }

    // label L2 = IF_END
    mVMWriter->writeLabel("IF_END" + std::to_string(localLabelCounter));

    // ifStatement
    mDepth--;
    this->write(std::string{"</ifStatement>"});
};

auto CompilationEngine::compileParameterList() -> int
{
    // parameterList
    this->write(std::string{"<parameterList>"});
    mDepth++;

    // Advance once
    mTokenizer->advance();

    // int A, int B ...
    mTokenizer->setKind(Kind::ARG);
    int nParameters = 0;
    while (mTokenizer->token() != std::string{")"})
    {
        this->write(mTokenizer->tokenType(), mTokenizer->token());
        mTokenizer->advance();
        nParameters++;
    }

    // parameterList
    mDepth--;
    this->write(std::string{"</parameterList>"});
    return nParameters;
}