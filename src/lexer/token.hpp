#pragma once
#include <string>

using namespace std;

enum class TokenType {
  Package,
  Import,

  Identifier,
  Integer,
  Float,
  String,

  Private,
  Final,

  // Type Keywords
  StringType,
  IntegerType,
  BooleanType,
  FloatType,
  ArrayType,

  // Operators & Delimiters
  Colon,
  Semicolon,
  Comma,
  Dot,
  LAngle,
  RAngle,
  Assign,

  // Control Flow
  If,
  For,
  In,
  While,
  Return,
  Else,
  Break,
  Continue,

  // Boolean
  True,
  False,

  // Modifiers
  Void,

  Method,
  Var,
  Println,
  Print,

  LBrace,
  RBrace,
  LParen,
  RParen,
  LBracket,
  RBracket,

  Equal,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,

  At, // @

  EqualEqual,   // ==
  NotEqual,     // !=
  EqualLess,    // =>
  LessEqual,    // <=
  EqualGreater, // =<
  GreaterEqual, // >=
  AndAnd,       // &&
  OrOr,         // ||
  Not,          // !

  EndOfFile
};

struct Token {
  TokenType type;
  string value;
  int line = 0;
  int column = 0;
};
