const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const vscode = require('vscode');

let client;

function activate(context) {
    const serverOptions = {
        run: {
            command: 'cubyte-lsp',
            args: [],
            transport: TransportKind.stdio,
        },
        debug: {
            command: 'cubyte-lsp',
            args: [],
            transport: TransportKind.stdio,
        }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'cubyte' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cbyte')
        }
    };

    client = new LanguageClient(
        'cubyte',
        'CuByte LSP',
        serverOptions,
        clientOptions
    );

    client.start();
}

function deactivate() {
    return client?.stop();
}

module.exports = { activate, deactivate };