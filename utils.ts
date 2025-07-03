// TypeScript file to test .ts extension support
export function formatString(str: string, prefix: string = ">>") {
    return prefix + " " + str;
}

export const VERSION = "2.0.0";

export default function defaultFunction() {
    return "This is from a TypeScript file";
}