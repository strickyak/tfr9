# notes/how-to-pmode4-in-basic.md

```basic
10 PMODE 4, 1
20 SCREEN 1, 1
30 PCLS
40 LINE (0, 0)-(255, 191), PSET
50 LINE (0, 191)-(255, 0), PSET
60 IF INKEY$ = "" THEN 60
```
