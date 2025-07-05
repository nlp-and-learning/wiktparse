In Wikitext, certain XML-style tags are treated specially by the parser. These are distinct from templates ({{...}}) and wikilinks ([[...]]), and typically affect rendering or parsing behavior.
🔹 Common Tags
Tag	Description
```
<!-- ... -->	Comment — ignored by the parser and not rendered.
<nowiki>...</nowiki>	Prevents interpretation of Wikitext markup inside (e.g., [[, {{).
<pre>...</pre>	Preserves all whitespace and line breaks; renders in monospaced font.
<code>...</code>	Renders inline code with monospace font (less used than <pre>).
<syntaxhighlight lang="...">...</syntaxhighlight>	Syntax-highlighted code block (requires SyntaxHighlight extension).
<ref>...</ref>	Citation footnote — content is rendered in <references /> section.
<references />	Displays all <ref> footnotes; can also be <references></references>.
<gallery>...</gallery>	Renders an image gallery.
<math>...</math>	Displays LaTeX-style math using Math extension.
<poem>...</poem>	Preserves formatting and line breaks in poetic content.
<includeonly>, <noinclude>, <onlyinclude>	Controls template transclusion.
<div>, <span>	HTML block/inline containers, often used with class or style.
<templatestyles src="...">	Loads CSS styles for a template (via TemplateStyles extension).
<small>, <big>, <sup>, <sub>	Text formatting tags (size/superscript/subscript).
```
Notes
* Self-closing tags are allowed: `<br />`, `<ref name="x" />`, etc.
* Tags are usually parsed before template and link expansion.
* Most tags can contain Wikitext, but `<nowiki>`, `<pre>`, `<syntaxhighlight>`, and `<math>` typically disable or escape it.
* Tags like `<noinclude>` and `<includeonly>` only affect template transclusion behavior, not rendering directly.
* Tags like `<nowiki>`, `<pre>`, `<!-- -->`, `<ref>`, etc. cannot be nested.
* The content inside such tags is treated as raw text until the first valid closing tag.
* The parser must stop parsing at the first occurrence of </tag>.

Difference between `<pre>` and `<nowiki>`
`<nowiki>...</nowiki>`

Purpose: Disables Wikitext parsing for the enclosed content.
- + Displays all content exactly as typed, including [[wikilinks]], {{templates}}, and formatting like ''italic'' or '''bold'''.
- - Comments (<!-- -->) are not parsed — they appear as plain text.
- - Whitespace and newlines are not preserved — content is shown inline.
- + Can be used inline (within a line of text).

Example:
```
<nowiki>{{Infobox}}</nowiki>
```

Displays:`{{Infobox}}`

`<pre>...</pre>`

Purpose: Displays preformatted text with preserved layout and disables parsing.
` + Wikitext is not parsed — like <nowiki>.
` + Comments (<!-- -->) are parsed and hidden (they work normally).
` + Preserves newlines, indentation, and shows content in monospaced font.
` - Not inline — always breaks into a separate block.

Example:
```
<pre>
<!-- hidden comment -->
{{Infobox}}
</pre>
```
Displays:
`{{Infobox}}`

(And hides the comment.)

Summary of Differences

| Feature                 | `<nowiki>`      | `<pre>`             |
|-------------------------|-----------------|---------------------|
| Wikitext parsing        | ❌ Disabled      | ❌ Disabled          |
| Comments (`<!--` `-->`) | ❌ Shown as text | ✅ Parsed and hidden |
| Preserves whitespace    | ❌ No            | ✅ Yes               |
| Monospaced formatting   | ❌ No            | ✅ Yes               |
| Inline usage            | ✅ Yes           | ❌ No                |

example
```
<pre>
<!-- This is hidden -->
{{Template}}
</pre>
```
display
```
{{Template}}
```


see https://www.mediawiki.org/wiki/Help:HTML_in_wikitext