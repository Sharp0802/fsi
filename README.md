# FSI : Fast Source Indexer

FSI is fast source indexer using ChromaDB, written with Rust.

## Method

Sources are chunked function-by-function.
And those chunked functions are stored into database.

Currently, Chunking is supported on only C/C++.
If a file is not written with C/C++,
Any chunking mechanism will be disabled.

But, If a language can be parsed by tree-sitter crate,
You can add a support for that language easily.

## Usage

```
fsi -i
fsi --index
fsi -q
fsi --query
```

- `-i` | `--index` - Index sources

Reads files from `stdin` line-by-line until EOF.
So you can pipe files to `fsi`:

```
find . -name '*.cxx' | fsi -i
```

- `-q` | `--query` - Query sources

Read-to-end `stdin`, and use it as query.
Same as indexing, you can also pipe query to `fsi`:

```
cat prompt.txt | fsi -q
fsi -q < prompt.txt
```
