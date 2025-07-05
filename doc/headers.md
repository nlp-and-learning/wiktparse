Each header is form:

===header===

* before text is m of '=', after is n of '='; 
* count k = min(m,n), if (k==0) is plain text;
* if (m>n) display "==header" with m-n of '=';
* if (n>m) display "header==" with n-m of '=';
* if after ==header== are spaces -> ignored
* if after ==header== are spaces ant text -> afer one space text 
and it is plain text, not header
* if before ==header== are spaces -> plain text, header must start with 
line begin