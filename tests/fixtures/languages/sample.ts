// Minimal TypeScript fixture for language parse-validation tests.

export interface Person {
    label: string;
}

export function add(a: number, b: number): number {
    return a + b;
}

export class Greeter implements Person {
    constructor(public label: string) {}

    greet(): string {
        return `Hello, ${this.label}`;
    }
}

export type Pair = [number, number];
