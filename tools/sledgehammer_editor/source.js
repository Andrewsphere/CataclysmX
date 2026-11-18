// Source locations belong to the text that was opened. The game validates every
// resulting document. This parser also retains the spans needed for small edits.
class Parser {
    constructor(text) {
        this.text = text;
        this.position = 0;
    }

    fail(message) {
        const line = this.text.slice(0, this.position).split(/\r\n|\r|\n/).length;
        throw new Error(message + " on line " + line);
    }

    skip() {
        for (;;) {
            while (/\s/u.test(this.text[this.position] ?? "")) this.position++;
            if (this.text.startsWith("//", this.position)) {
                while (this.position < this.text.length && !/[\r\n\u2028\u2029]/u.test(this.text[this.position])) {
                    this.position++;
                }
            } else if (this.text.startsWith("/*", this.position)) {
                const end = this.text.indexOf("*/", this.position + 2);
                if (end < 0) this.fail("Unclosed comment");
                this.position = end + 2;
            } else {
                return;
            }
        }
    }

    string() {
        const quote = this.text[this.position++];
        let value = "";
        const escapes = {b: "\b", f: "\f", n: "\n", r: "\r", t: "\t", v: "\v", "0": "\0"};
        while (this.position < this.text.length) {
            const ch = this.text[this.position++];
            if (ch === quote) return value;
            if (ch !== "\\") {
                value += ch;
                continue;
            }
            const escape = this.text[this.position++];
            if (escape === "x" || escape === "u") {
                const length = escape === "x" ? 2 : 4;
                const digits = this.text.slice(this.position, this.position + length);
                if (digits.length !== length || !/^[0-9a-f]+$/i.test(digits)) this.fail("Invalid escape");
                value += String.fromCharCode(parseInt(digits, 16));
                this.position += length;
            } else if (escape === "\r") {
                if (this.text[this.position] === "\n") this.position++;
            } else if (escape !== "\n" && escape !== "\u2028" && escape !== "\u2029") {
                value += escapes[escape] ?? escape;
            }
        }
        this.fail("Unclosed string");
    }

    token() {
        const start = this.position;
        while (this.position < this.text.length && !/[\s,:\]}\/]/u.test(this.text[this.position])) {
            this.position++;
        }
        if (start === this.position) this.fail("Expected a value");
        return this.text.slice(start, this.position);
    }

    container(open) {
        const array = open === "[";
        const close = array ? "]" : "}";
        const children = [];
        const value = array ? [] : Object.create(null);
        this.position++;
        this.skip();
        while (this.text[this.position] !== close) {
            const start = this.position;
            const key = array ? children.length :
                this.text[this.position] === "'" || this.text[this.position] === '"' ? this.string() :
                    this.token().replace(/\\u([0-9a-f]{4})/gi, (_, digits) => String.fromCharCode(parseInt(digits, 16)));
            if (!array) {
                this.skip();
                if (this.text[this.position++] !== ":") this.fail("Expected a colon");
                if (Object.hasOwn(value, key)) this.fail("Duplicate field " + key);
            }
            const node = this.value();
            const child = {start, key, node, comma: null};
            value[key] = node.value;
            children.push(child);
            this.skip();
            if (this.text[this.position] !== ",") {
                if (this.text[this.position] !== close) this.fail("Expected a comma or " + close);
                break;
            }
            child.comma = this.position++;
            this.skip();
        }
        this.position++;
        return {value, children};
    }

    value() {
        this.skip();
        const start = this.position;
        const ch = this.text[start];
        let parsed;
        if (ch === "[" || ch === "{") {
            parsed = this.container(ch);
        } else if (ch === "'" || ch === '"') {
            parsed = {value: this.string()};
        } else {
            const token = this.token();
            if (token === "true" || token === "false") parsed = {value: token === "true"};
            else if (token === "null") parsed = {value: null};
            else if (/^[+-]?(?:[0-9]+|0x[0-9a-f]+)$/i.test(token)) {
                const unsigned = token.replace(/^[+-]/, "");
                const number = BigInt(unsigned) * (token[0] === "-" ? -1n : 1n);
                parsed = {value: Number.isSafeInteger(Number(number)) ? Number(number) : number};
            } else if (/^[+-]?(?:Infinity|NaN|(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)$/i.test(token)) {
                parsed = {value: Number(token)};
            } else {
                this.fail("Unexpected token " + token);
            }
        }
        return {...parsed, start, end: this.position};
    }

    read() {
        const root = this.value();
        this.skip();
        if (this.position !== this.text.length) this.fail("Unexpected text");
        return root;
    }
}

export function parseValue(text) {
    return new Parser(text).read().value;
}

export function equal(left, right) {
    if (Object.is(left, right)) return true;
    if (!left || !right || typeof left !== "object" || typeof right !== "object"
        || Array.isArray(left) !== Array.isArray(right)) return false;
    const keys = Object.keys(left);
    return keys.length === Object.keys(right).length &&
        keys.every(key => Object.hasOwn(right, key) && equal(left[key], right[key]));
}

export function spelling(value, indent = "", newline = "\n") {
    if (typeof value === "bigint" || typeof value === "number") return String(value);
    if (typeof value !== "object" || value === null) return JSON.stringify(value);
    const array = Array.isArray(value);
    const open = array ? "[" : "{";
    const close = array ? "]" : "}";
    const nextIndent = indent + "  ";
    const entries = Object.entries(value);
    if (!entries.length) return open + close;
    return open + newline + entries.map(([key, entry]) =>
        nextIndent + (array ? "" : JSON.stringify(key) + ": ") + spelling(entry, nextIndent, newline)
    ).join("," + newline) + newline + indent + close;
}

function isContainer(value, array) {
    return value && typeof value === "object" && Array.isArray(value) === array;
}

function rowSpelling(value, quote) {
    const escaped = JSON.stringify(value);
    if (quote !== "'") return escaped;
    return "'" + escaped.slice(1, -1).replace(/\\"/g, '"').replace(/'/g, "\\'") + "'";
}

export class SourceFile {
    constructor(path, text, editable = true) {
        this.path = path;
        this.before = text;
        this.editable = editable;
        this.original = text === null ? null : new Parser(text).read();
        this.value = this.original?.value;
        this.newline = text?.includes("\r\n") ? "\r\n" : "\n";
    }

    indentation(position) {
        const line = this.before.lastIndexOf("\n", position - 1) + 1;
        return /^[ \t]*/.exec(this.before.slice(line, position))[0];
    }

    replacement(node, value, edits) {
        if (equal(node.value, value)) return;
        if (!node.children || !isContainer(value, Array.isArray(node.value))) {
            const quote = this.before[node.start];
            const text = typeof value === "string" ? rowSpelling(value, quote) :
                spelling(value, this.indentation(node.start), this.newline);
            edits.push({start: node.start, end: node.end, text});
            return;
        }
        if (Array.isArray(value)) this.arrayEdits(node, value, edits);
        else this.objectEdits(node, value, edits);
    }

    remove(child, edits) {
        edits.push({start: child.start, end: child.node.end, text: ""});
        if (child.comma !== null) edits.push({start: child.comma, end: child.comma + 1, text: ""});
    }

    insert(node, values, next, last, edits, isArray) {
        if (!values.length) return;
        const close = node.end - 1;
        const indentation = this.indentation(node.start);
        const reference = node.children[0];
        const childIndent = reference && this.indentation(reference.start).length > indentation.length ?
            this.indentation(reference.start) : indentation + "  ";
        const entries = values.map(([key, value]) =>
            (isArray ? "" : JSON.stringify(key) + ": ") + spelling(value, childIndent, this.newline));
        if (next) {
            const text = entries.join("," + this.newline + childIndent) + "," + this.newline + childIndent;
            edits.push({start: next.start, end: next.start, text});
            return;
        }
        if (last && last.comma === null) {
            edits.push({start: last.node.end, end: last.node.end, text: ","});
        }
        const text = this.newline + childIndent + entries.join("," + this.newline + childIndent) +
            "," + this.newline + indentation;
        const closeLine = this.before.lastIndexOf("\n", close - 1) + 1;
        const ownLine = closeLine > node.start && /^[ \t]*$/.test(this.before.slice(closeLine, close));
        edits.push({start: ownLine ? closeLine : close, end: close, text: ownLine ? text.slice(this.newline.length) : text});
    }

    objectEdits(node, value, edits) {
        let last;
        for (const child of node.children) {
            if (Object.hasOwn(value, child.key)) {
                this.replacement(child.node, value[child.key], edits);
                last = child;
            } else {
                this.remove(child, edits);
            }
        }
        const additions = Object.entries(value).filter(([key]) => !Object.hasOwn(node.value, key));
        this.insert(node, additions, null, last, edits, false);
    }

    arrayEdits(node, value, edits) {
        if (value.length === node.children.length) {
            for (const child of node.children) this.replacement(child.node, value[child.key], edits);
            return;
        }
        let first = 0;
        let oldEnd = node.children.length;
        let newEnd = value.length;
        while (first < oldEnd && first < newEnd && equal(node.value[first], value[first])) first++;
        while (first < oldEnd && first < newEnd && equal(node.value[oldEnd - 1], value[newEnd - 1])) {
            oldEnd--;
            newEnd--;
        }
        const commonEnd = Math.min(oldEnd, newEnd);
        for (let index = first; index < commonEnd; index++) this.replacement(node.children[index].node, value[index], edits);
        for (let index = commonEnd; index < oldEnd; index++) this.remove(node.children[index], edits);
        const additions = value.slice(commonEnd, newEnd).map(entry => ["", entry]);
        this.insert(node, additions, node.children[oldEnd], node.children[oldEnd - 1], edits, true);
    }

    edit() {
        if (this.value === undefined) return null;
        if (!this.original) return {path: this.path, before: null, text: spelling(this.value) + this.newline};
        if (equal(this.original.value, this.value)) return null;
        const replacements = [];
        this.replacement(this.original, this.value, replacements);
        let text = this.before;
        // At a shared boundary, apply the later insertion first so a comma
        // inserted at the end of a value precedes the appended entries.
        const ordered = replacements.map((edit, index) => ({...edit, index})).sort((a, b) =>
            b.start - a.start || b.end - a.end || b.index - a.index);
        for (const {start, end, text: inserted} of ordered) text = text.slice(0, start) + inserted + text.slice(end);
        return {path: this.path, before: this.before, text};
    }
}
