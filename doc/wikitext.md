Template: https://en.wikipedia.org/wiki/Help:Template
Whitespace is trimmed from the start and end of named parameters
but not unnamed parameters. 
See Help:Template#Whitespace handling.

Whitespace handling

To expand on that: for named parameters, such as all of those you list above, whitespace is insignificant:
* after the pipe that precedes the parameter name
* after the parameter name
* after the equals
* after the parameter value.
Whitespace is always significant within the parameter name
Whitespace may be significant within the parameter value
But for unnamed (positional) parameters, all whitespace is significant.


Leading and trailing whitespace (including line breaks) 
around named parameters and values is collapsed 
automatically, but spaces in the middle of the value
are kept. 

For instance, {{ ... | myparam = this is a test }} has the same output as {{ ... |myparam=this is a test}}. 
However, with unnamed parameters, all whitespace is retained as preserved in the output.

The collapsing of line breaks around parameters can be used to improve the readability of a template 
call with many parameters by placing each 
parameter specification in its own line.

Template parameter key rules in Wikitext:
* Leading and trailing whitespace characters (spaces, tabs, \n, \r) are trimmed.
* Internal spaces and tabs are preserved (e.g., some key is valid).
* Newlines (\n) and carriage returns (\r) are not allowed inside the key — they terminate the key and the line.
* Therefore, keys must be a single line, even if the template itself spans multiple lines.

Template name rules ({{TemplateName...}}):
* The template name is the text immediately after {{, up to the first |, }}, or newline.
* Leading and trailing whitespace is trimmed.
* Internal whitespace (including spaces and tabs) is preserved.
* Newlines (\n) are not allowed in the template name — they terminate the name.
* Example
```
{{  Infobox language  |key=value}} → name: "Infobox language"
```

Parser function name rules ({{#FunctionName:...}}):
* A parser function must start with {{#, followed immediately by a function name, followed by a colon :.
Any newline before the colon terminates the function name and results in a syntax error — the call is no longer recognized as a parser function.
Leading and trailing whitespace around the function name (before the colon) is trimmed.
* Leading/trailing whitespace is trimmed.
* Internal whitespace is preserved (though function names usually don’t contain it).
* Must be followed by a colon : — otherwise parsing fails.
* Newlines (\n) are not allowed in parser function names.
* Example:
```
{{#invoke:Chart|...}} → function name: "invoke"
```

One \n is replaced by space, but two \n (one empty line) 
is replaced by \n; n+1 breaks (=n empty lines) is replaced 
by n breaks

Infobox language: https://en.wikipedia.org/wiki/Template:Infobox_language