/** AST node definitions for the Novus language server. */
import { Span } from './lexer';

export type { Span };

export interface NamedArg extends Span {
  kind: 'NamedArg';
  name: string;
  nameSpan: Span;
  value?: Expr;
}

export interface Annotation extends Span {
  kind: 'Annotation';
  name: string;
  nameSpan: Span;
  args: NamedArg[];
}

export interface TypeRef extends Span {
  kind: 'TypeRef';
  name: string;
  nameSpan: Span;
  args: TypeRef[];
}

export interface Param extends Span {
  kind: 'Param';
  type?: TypeRef;
  name: string;
  nameSpan: Span;
}

export interface ModifierTok extends Span {
  name: string;
}

export interface Accessor extends Span {
  name: 'get' | 'set';
}

export interface PackageDecl extends Span {
  kind: 'Package';
  name: string;
  nameSpan: Span;
}

export interface ImportDecl extends Span {
  kind: 'Import';
  name: string;
  nameSpan: Span;
  /** Set for file imports: `import "file.nv"`. */
  isFile?: boolean;
  /** The raw path of a file import. */
  path?: string;
}

export interface DeclBase extends Span {
  annotations: Annotation[];
  modifiers: ModifierTok[];
  doc?: string;
  name: string;
  nameSpan: Span;
}

export interface MethodDecl extends DeclBase {
  kind: 'Method';
  params: Param[];
  returnType?: TypeRef;
  body?: Block;
  isConstructor: boolean;
  /** Whether the declaration used the `method` keyword (interface members may omit it). */
  hasKeyword: boolean;
  keywordSpan?: Span;
}

export interface FieldDecl extends DeclBase {
  kind: 'Field';
  type?: TypeRef;
  accessors: Accessor[];
  value?: Expr;
  /** Declared with the `var` keyword. */
  isVar: boolean;
  varSpan?: Span;
}

export interface EnumConstant extends Span {
  kind: 'EnumConstant';
  name: string;
  nameSpan: Span;
  args: Expr[];
}

export type DefineKind = 'class' | 'enum' | 'interface' | 'abstract' | 'annotation';

export interface DefineDecl extends DeclBase {
  kind: 'Define';
  defineKind: DefineKind;
  params: Param[];
  bases: TypeRef[];
  constants: EnumConstant[];
  members: Member[];
  bodySpan?: Span;
}

export type Member = MethodDecl | FieldDecl | DefineDecl;

export interface Block extends Span {
  kind: 'Block';
  statements: Stmt[];
}

export interface PrintStmt extends Span {
  kind: 'Print';
  value?: Expr;
}

export interface ReturnStmt extends Span {
  kind: 'Return';
  value?: Expr;
}

export interface IfStmt extends Span {
  kind: 'If';
  cond?: Expr;
  then?: Stmt;
  else?: Stmt;
}

export interface ForStmt extends Span {
  kind: 'For';
  /** for (item in items) */
  varName?: string;
  varSpan?: Span;
  iterable?: Expr;
  /** for (init; cond; update) */
  init?: Stmt;
  cond?: Expr;
  update?: Stmt;
  body?: Stmt;
}

export interface WhileStmt extends Span {
  kind: 'While';
  cond?: Expr;
  body?: Stmt;
}

export interface BreakStmt extends Span {
  kind: 'Break';
}

export interface ContinueStmt extends Span {
  kind: 'Continue';
}

export interface ExprStmt extends Span {
  kind: 'ExprStmt';
  expr: Expr;
}

export interface AssignStmt extends Span {
  kind: 'Assign';
  target: Expr;
  opSpan: Span;
  value?: Expr;
}

export type Stmt =
  | Block
  | PrintStmt
  | ReturnStmt
  | IfStmt
  | ForStmt
  | WhileStmt
  | BreakStmt
  | ContinueStmt
  | ExprStmt
  | AssignStmt
  | Member;

export interface Ident extends Span {
  kind: 'Ident';
  name: string;
}
export interface ThisExpr extends Span {
  kind: 'This';
}
export interface IntLit extends Span {
  kind: 'Int';
  value: number;
}
export interface FloatLit extends Span {
  kind: 'Float';
  value: number;
}
export interface BoolLit extends Span {
  kind: 'Bool';
  value: boolean;
}
export interface NullLit extends Span {
  kind: 'Null';
}
export interface StringLit extends Span {
  kind: 'String';
  value: string;
  interpolations: Expr[];
}
export interface ArrayLit extends Span {
  kind: 'Array';
  elements: Expr[];
}
export interface MapEntry extends Span {
  key: Expr;
  value?: Expr;
}
export interface MapLit extends Span {
  kind: 'Map';
  entries: MapEntry[];
}
/** `Person{ name="Tom", age=18 }` */
export interface StructInit extends Span {
  kind: 'StructInit';
  target: Expr;
  args: NamedArg[];
}
export interface MemberExpr extends Span {
  kind: 'Member';
  object: Expr;
  name: string;
  nameSpan: Span;
}
export interface CallExpr extends Span {
  kind: 'Call';
  callee: Expr;
  args: Expr[];
  parenStart: number;
  parenEnd: number;
}
export interface IndexExpr extends Span {
  kind: 'Index';
  object: Expr;
  index?: Expr;
}
export interface BinaryExpr extends Span {
  kind: 'Binary';
  op: string;
  opSpan: Span;
  left: Expr;
  right?: Expr;
}
export interface UnaryExpr extends Span {
  kind: 'Unary';
  op: string;
  operand?: Expr;
}
export interface ParenExpr extends Span {
  kind: 'Paren';
  expr?: Expr;
}

export type Expr =
  | Ident
  | ThisExpr
  | IntLit
  | FloatLit
  | BoolLit
  | NullLit
  | StringLit
  | ArrayLit
  | MapLit
  | StructInit
  | MemberExpr
  | CallExpr
  | IndexExpr
  | BinaryExpr
  | UnaryExpr
  | ParenExpr;

export type TopLevel = PackageDecl | ImportDecl | Stmt;

export interface Program extends Span {
  kind: 'Program';
  items: TopLevel[];
}

export type Node = Program | TopLevel | Expr | Annotation | TypeRef | Param | NamedArg | EnumConstant;

export function isExpr(node: Node): node is Expr {
  switch (node.kind) {
    case 'Ident': case 'This': case 'Int': case 'Float': case 'Bool': case 'Null': case 'String':
    case 'Array': case 'Map': case 'StructInit': case 'Member': case 'Call': case 'Index':
    case 'Binary': case 'Unary': case 'Paren':
      return true;
    default:
      return false;
  }
}

/** Returns the direct child nodes of `node` in source order (roughly). */
export function children(node: Node): Node[] {
  const out: Node[] = [];
  const add = (n?: Node): void => {
    if (n) out.push(n);
  };
  switch (node.kind) {
    case 'Program':
      node.items.forEach(add);
      break;
    case 'Package':
    case 'Import':
    case 'Break':
    case 'Continue':
    case 'Ident':
    case 'This':
    case 'Int':
    case 'Float':
    case 'Bool':
    case 'Null':
      break;
    case 'Annotation':
      node.args.forEach(add);
      break;
    case 'NamedArg':
      add(node.value);
      break;
    case 'TypeRef':
      node.args.forEach(add);
      break;
    case 'Param':
      add(node.type);
      break;
    case 'Method':
      node.annotations.forEach(add);
      node.params.forEach(add);
      add(node.returnType);
      add(node.body);
      break;
    case 'Field':
      node.annotations.forEach(add);
      add(node.type);
      add(node.value);
      break;
    case 'Define':
      node.annotations.forEach(add);
      node.params.forEach(add);
      node.bases.forEach(add);
      node.constants.forEach(add);
      node.members.forEach(add);
      break;
    case 'EnumConstant':
      node.args.forEach(add);
      break;
    case 'Block':
      node.statements.forEach(add);
      break;
    case 'Print':
    case 'Return':
      add(node.value);
      break;
    case 'If':
      add(node.cond);
      add(node.then);
      add(node.else);
      break;
    case 'For':
      add(node.init);
      add(node.iterable);
      add(node.cond);
      add(node.update);
      add(node.body);
      break;
    case 'While':
      add(node.cond);
      add(node.body);
      break;
    case 'ExprStmt':
      add(node.expr);
      break;
    case 'Assign':
      add(node.target);
      add(node.value);
      break;
    case 'String':
      node.interpolations.forEach(add);
      break;
    case 'Array':
      node.elements.forEach(add);
      break;
    case 'Map':
      node.entries.forEach(e => {
        add(e.key);
        add(e.value);
      });
      break;
    case 'StructInit':
      add(node.target);
      node.args.forEach(add);
      break;
    case 'Member':
      add(node.object);
      break;
    case 'Call':
      add(node.callee);
      node.args.forEach(add);
      break;
    case 'Index':
      add(node.object);
      add(node.index);
      break;
    case 'Binary':
      add(node.left);
      add(node.right);
      break;
    case 'Unary':
      add(node.operand);
      break;
    case 'Paren':
      add(node.expr);
      break;
  }
  return out;
}

/** Path of nodes from the root down to the innermost node containing `offset`. */
export function findNodePath(root: Node, offset: number): Node[] {
  const path: Node[] = [];
  let current: Node | undefined = root;
  while (current) {
    path.push(current);
    let next: Node | undefined;
    for (const child of children(current)) {
      if (child.start <= offset && offset <= child.end) {
        // prefer the last child that contains the offset (tightest on ties)
        if (!next || child.end - child.start <= next.end - next.start) next = child;
      }
    }
    current = next;
  }
  return path;
}

export function walk(node: Node, visit: (n: Node, parent?: Node) => void | boolean, parent?: Node): void {
  if (visit(node, parent) === false) return;
  for (const child of children(node)) walk(child, visit, node);
}
