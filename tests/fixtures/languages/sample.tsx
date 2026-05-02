// Minimal TSX fixture for language parse-validation tests.

import * as React from "react";

export interface GreetingProps {
    label: string;
}

export function Greeting({ label }: GreetingProps) {
    return <h1>Hello, {label}</h1>;
}

export class Counter extends React.Component<{}, { n: number }> {
    state = { n: 0 };

    render() {
        return <span>{this.state.n}</span>;
    }
}
