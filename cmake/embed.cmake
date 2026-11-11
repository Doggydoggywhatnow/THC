# embed_file(<input> <output.c> <symbol>): turn a binary file into a C array.
function(embed_file input output symbol)
  file(READ "${input}" hex HEX)
  string(LENGTH "${hex}" hexlen)
  math(EXPR len "${hexlen} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
  # break long lines every 32 bytes
  string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" body "${body}")
  file(WRITE "${output}"
    "/* Generated from ${input}. Do not edit. */\n"
    "const unsigned char ${symbol}[] = {\n${body}\n};\n"
    "const unsigned int ${symbol}_len = ${len};\n")
endfunction()
