# Parser

The 'parser' here is a Yaml-ish parser for the configuration settings
of FluidNC. Note the 'ish' part here; while most syntax highlighters
for Yaml will work just fine, we don't support all yaml quirks like 
references. 

The parser was designed to be lean-and-simple, specifically with the 
goal of not using too much memory. 

The parser is split in multiple parts: 

1. A tokenizer.
2. A yaml parser.
3. A tree builder.

For reference in this document, consider this yaml document:

```yaml
name: "Debug config"
board: Debug-board

axes:
  number_axis: 3

coolant:
  flood: gpio.2
```

Initially, the whole yaml file is read to memory (or memory mapped 
to a `const char*`). Next, it is processed token-by-token.

## Tokenizer

Can be found in `Tokenizer.h` / `Tokenizer.cpp`. The tokenizer 
splits the yaml file in single tokens. A single token in a yaml 
file is basically a struct containing:

- Indentation (count)
- A key
- A value [optional]
- The token kind

Comments are stripped out of the yaml file, as are newlines and 
empty lines. There are a few different value types, which can be 
differentiated by the token kind:

- Strings
- Integer values (numbers without a '.' or 'e'/'E')
- Float values (everything with a '.' or 'e'/'E')
- Bool values

Both keys and string values are two `const char*` within the 
original yaml code. Sections have (like `axis` above) have no
value.

## Parser

Can be found in `Parser.h` / `Parser.cpp`. The parser is responsible
for handling the hierarchical nature of the yaml files, and provides
some convenient way to enumerate it.

It is important to note at this point, that the tree builder won't just 
randomly traverse through the configuration file. All keys that are 
sections or available, are known by the builder, and mapped on real
classes. So, irrelevant sections can be skipped immediately, while 
relevant sections can be 'entered' and 'left' when done.

The parser therefore just supports the enumeration, and knows how to 
skip tokens or whole sections. It depends on the tree builder what 
happens: 
- If the tree builder calls `moveNext`, then the next key _within_ 
  the current section is searched. If such a thing is available, `true`
  is returned, otherwise `false`.
- If the tree builder calls `enter` and the current key is a section,
  the parser moves to enumerating the section nodes until the 
  `leave` call is invoked.

It is up to the tree builder to actually make heads and tails of this, 
and ensure that nodes are created.

## Tree building

There is a configuration handler for yaml files called `ParserHandler.h`,
which implements the [ConfigurationHandler](ConfigurationHandlers.md) 
concept.

After running the tree builder, a fixup step is required to complete the 
configuration with missing parts, and normally a validation 
step is is required to check if everything is in order.
