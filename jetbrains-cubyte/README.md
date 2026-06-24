# Cubyte JetBrains Plugin

Skeleton JetBrains IDE plugin for the Cubyte programming language.

## What it does

- Associates `.cbyte` files with the **Cubyte** language (icon, name).
- Provides **syntax highlighting** for `.cbyte` files using the
  TextMate grammar `cubyte.tmLanguage.json` (the same one used by the
  VS Code extension and the Cubyte visualizer, copied verbatim).
- Does **not** yet provide language server features (completions,
  diagnostics, go-to-definition). Those require installing
  `cubyte-lsp` from PyPI and wiring up a `LspServerDescriptor` —
  see *Status* below.

## Status

Skeleton only. The Kotlin classes contain TODO comments documenting
how each piece fits together. Highlighting works out of the box once
the plugin is loaded into a JetBrains IDE; semantic features do not.

## Building

The plugin is a standard Gradle project using the official
`org.jetbrains.intellij.platform` plugin. From this directory:

```bash
# Launch a sandboxed IDE with the plugin pre-installed for testing.
./gradlew runIde

# Produce a distributable .zip in build/distributions/.
./gradlew buildPlugin
```

The first build downloads the IntelliJ IDEA Community distribution
that the plugin is compiled against (set in `build.gradle.kts`).

## Installing the built plugin

1. Run `./gradlew buildPlugin`.
2. In your JetBrains IDE, open `Settings → Plugins`.
3. Click the gear icon and choose **Install Plugin from Disk…**.
4. Select `build/distributions/jetbrains-cubyte-0.0.1.zip`.

## Project layout

```
jetbrains-cubyte/
├── build.gradle.kts                # Gradle build configuration
├── settings.gradle.kts             # Gradle project name & repos
├── gradle.properties               # Plugin id/name/version
├── cubyte-icon.png                 # Source icon (VS Code original)
└── src/main/
    ├── kotlin/com/cubyte/lang/     # Kotlin sources (skeletons)
    │   ├── CubyteFileType.kt       # .cbyte → Cubyte language
    │   ├── CubyteIcons.kt          # Icon registry
    │   ├── CubyteLanguage.kt       # Language id singleton
    │   ├── CubyteParserDefinition.kt # Minimal PSI parser
    │   ├── CubyteSyntaxHighlighter.kt # No-op highlighter fallback
    │   └── CubyteSyntaxHighlighterFactory.kt
    └── resources/
        ├── META-INF/plugin.xml     # Plugin manifest
        ├── icons/cubyte-icon.png   # Bundled plugin icon
        └── textMate/cubyte.tmLanguage.json  # TextMate grammar
```

## How the TextMate highlighting works

IntelliJ's platform includes a TextMate layer (`com.intellij.platform.textMate`)
that parses TextMate grammars written for VS Code. The platform
parses the JSON, compiles the Oniguruma regexes, and applies tokens
to `.cbyte` files in the editor — including support for `begin`/`end`
regions, `beginCaptures`/`endCaptures`, and the dotted scope names
that drive theme-based coloring.

The grammar in `cubyte.tmLanguage.json` is the same file used by:

- `vscode-cubyte` (the VS Code extension at `../vscode-cubyte/`).
- `visualiser/cubyte-highlight.js` (the in-browser visualizer).

So the same tokens, the same colors (via each editor's default
themes), and the same scope-based class names carry across all
three environments.

## Roadmap (TODOs in the code)

- **LSP integration**: register an `LspServerDescriptor` that spawns
  `cubyte-lsp` (from PyPI) over stdio for diagnostics, completion,
  and go-to-definition. Add a `<platform.lsp.server>` extension in
  `plugin.xml`.
- **Real PSI parser**: replace the empty `CubyteParserDefinition`
  with a Grammar-Kit-generated parser if/when IDE features beyond
  highlighting (find usages, refactor) are wanted.
- **Brace matching / comment toggling**: register a matching
  companion to `CubyteParserDefinition` once a real lexer is in
  place.