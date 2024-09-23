#ifndef AST_H
#define AST_H

#include <string>
#include <vector>
#include <memory>

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual std::string toString() const = 0;
};

class Expression : public ASTNode {
    // Add expression-specific methods
};

class Statement : public ASTNode {
    // Add statement-specific methods
};

class FunctionDefinition : public ASTNode {
public:
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> parameters;
    std::vector<std::unique_ptr<Statement>> body;

    std::string toString() const override {
        return "Function: " + name;
    }
};

// Add more AST node types as needed

#endif // AST_H
