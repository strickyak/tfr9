all: cross-compile

STATICLY =    -ldflags '-extldflags "-static"'

GO_BUILD = go build --tags=level2,coco3 -x

cross-compile:
	GOOS=linux GOARCH=amd64       $(GO_BUILD) $(STATICLY) -o tconsole.linux-amd64.exe tconsole.go
	GOOS=linux GOARCH=386         $(GO_BUILD) $(STATICLY) -o tconsole.linux-386.exe tconsole.go
	GOOS=linux GOARCH=arm GOARM=7 $(GO_BUILD) $(STATICLY) -o tconsole.linux-arm-7.exe tconsole.go
	GOOS=linux GOARCH=arm64       $(GO_BUILD) $(STATICLY) -o tconsole.linux-arm64.exe tconsole.go
	GOOS=windows GOARCH=amd64     $(GO_BUILD) $(STATICLY) -o tconsole.win-amd64.exe tconsole.go
	GOOS=windows GOARCH=386       $(GO_BUILD) $(STATICLY) -o tconsole.win-386.exe tconsole.go
	GOOS=darwin GOARCH=arm64      $(GO_BUILD) $(STATICLY) -o tconsole.mac-arm64.exe tconsole.go
	GOOS=darwin GOARCH=amd64      $(GO_BUILD) $(STATICLY) -o tconsole.mac-amd64.exe tconsole.go
