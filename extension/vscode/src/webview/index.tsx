// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Webview entry point — bundled by esbuild into out/webview.js.

import * as React from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';

const container = document.getElementById('root');
if (!container) {
    throw new Error('Vectra: webview root element missing.');
}
createRoot(container).render(<App />);
