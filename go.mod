module github.com/strickyak/tfr9

go 1.25.1

replace github.com/strickyak/copico-centipede => ./v3/merging/centipede

require (
	github.com/gorilla/websocket v1.5.3
	github.com/jacobsa/go-serial v0.0.0-20180131005756-15cf729a72d4
	github.com/microcosm-cc/bluemonday v1.0.27
	github.com/russross/blackfriday v1.6.0
	github.com/strickyak/copico-centipede v0.0.0
	github.com/strickyak/gomar v0.0.0-20240628194527-5983f6cb5e90
	golang.org/x/sys v0.26.0
)

require (
	github.com/aymerick/douceur v0.2.0 // indirect
	github.com/gorilla/css v1.0.1 // indirect
	golang.org/x/net v0.26.0 // indirect
)
