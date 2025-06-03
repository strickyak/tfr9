package main

// var Sources = make(map[string]*listings.ModSrc)

func AsmSourceLine(modName string, offset uint) string {
	return ""
	/*
	   modsrc, ok := Sources[modName]

	   	if !ok {
	   		modsrc = listings.LoadFile(*listings.Borges + "/" + modName)
	   		Sources[modName] = modsrc
	   	}

	   	if modsrc == nil {
	   		Sources[modName] = &listings.ModSrc{
	   			Src:      make(map[uint]string),
	   			Filename: modName,
	   		}
	   		return ""
	   	}

	   srcLine, _ := modsrc.Src[offset]
	   return srcLine
	*/
}
