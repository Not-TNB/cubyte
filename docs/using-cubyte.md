# Using `cubyte`

This is the practical path from source code to compiled CuBit assembly.

## File Types

- `.cbyte` is the source file you write.
- `-pp.cbyte` is a generated preprocessor file. The compiler creates it by stripping comments before parsing. Do not edit it; it is safe to ignore or delete.
- `.cubin` is the compiled assembly output.

For example:

```text
extension/examples/prompt_echo.cbyte      source
extension/examples/prompt_echo-pp.cbyte   generated intermediate
extension/examples/prompt_echo.cubin      compiled output
```

## Build The Compiler

From the repo root:

```bash
make -C extension all
```

This builds:

```text
extension/cubyte
```

## Compile A Program

Pass the input path without `.cbyte`, then the output `.cubin` path:

```bash
./extension/cubyte extension/examples/prompt_echo extension/examples/prompt_echo.cubin
```

The compiler reads:

```text
extension/examples/prompt_echo.cbyte
```

and writes:

```text
extension/examples/prompt_echo.cubin
```

It also creates:

```text
extension/examples/prompt_echo-pp.cbyte
```

That `-pp.cbyte` file is only a temporary preprocessed copy.

## Useful Debug Flags

Flags can go before the input path:

```bash
./extension/cubyte --dump-ast extension/examples/prompt_echo /tmp/out.cubin
./extension/cubyte --dump-desugar extension/examples/prompt_echo /tmp/out.cubin
./extension/cubyte --dump-regs extension/examples/prompt_echo /tmp/out.cubin
```

Common flags:

- `--dump-ast`: print the parsed source AST.
- `--dump-desugar`: print the lower-level program after desugaring.
- `--dump-cfg`: print the control-flow graph.
- `--dump-liveness`: print liveness information.
- `--dump-ig`: print the interference graph.
- `--dump-regs`: show which source variables map to which physical cube registers.

## Source Basics

Integer variables are backed by cube registers:

```cbyte
let int : 3 score := 1;
score := score + 1;
output score;
```

The number after `:` is the required register order. Smaller orders are easier for the current allocator to find.

Input uses a prompt string and writes the runtime value into the `_io` register:

```cbyte
input "enter alg: ";
output;
```

Use bare `output;` or `output _io;` for the I/O register. `_io` is a reserved source variable that always binds to physical `R0`; ordinary variables cannot redeclare it.

You can also manipulate it directly:

```cbyte
input "enter alg: ";
_io := _io + 1;
output _io;
```

Branches and loops currently work best with equality, `not`, and `solved [...]` conditions:

```cbyte
while not (rounds = 3) {
    rounds := rounds + 1;
}

if solved [UF, DF, FL, FR, BL] {
    output rounds;
}
```

Algorithm values can be used with `apply`:

```cbyte
let alg setup := "R U R' U'";
apply setup;
```

## Example

Source:

```cbyte
let int : 3 rounds := 0;
let int : 3 score := 1;

while not (rounds = 3) {
    rounds := rounds + 1;
    score := score + rounds;
}

if solved [UF, DF, FL, FR, BL] {
    score := score + 1;
} else {
    score := score + 2;
}

output score;

input "enter alg: ";
output;
```

Compile it:

```bash
make -C extension all
./extension/cubyte extension/examples/prompt_echo extension/examples/prompt_echo.cubin
```

Inspect the output:

```bash
sed -n '1,80p' extension/examples/prompt_echo.cubin
```
