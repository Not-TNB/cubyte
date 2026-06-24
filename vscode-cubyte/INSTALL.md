# Installing the Cubyte VS Code Extension

To use the Cubyte language extension in VS Code, you need to complete two steps: install the extension from the `.vsix` file, and install the Cubyte language server from PyPI.

## 1. Install the extension

Download the `.vsix` file from this repository (`vscode-cubyte-0.0.1.vsix`) and install it in VS Code:

```bash
code --install-extension vscode-cubyte-0.0.1.vsix
```

Alternatively, open VS Code, go to the Extensions view, click the `…` menu, choose **Install from VSIX…**, and select the file.

## 2. Install the language server

The extension depends on `cubyte-lsp`, which must be installed separately from PyPI:

```bash
pip install cubyte-lsp
```

Make sure the `cubyte-lsp` command is available on your `PATH` so VS Code can launch the language server.

## That's it

Once both are installed, open any `.cbyte` file and you'll get Cubyte syntax highlighting and language server features (diagnostics, completions, etc.).