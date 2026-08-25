package main

var Sources = make(map[string]*ModSrc)

func AsmSourceLine(modName string, offset uint) string {
	if modName == "" && LinkSrc != nil {
		if rec, ok := LinkSrc.Src[offset]; ok {
			return rec
		}
	}

	if modName == "^^" && LinkSrc != nil {
		if rec, ok := LinkSrc.Src[offset]; ok {
			return rec
		}
	}

	modsrc, ok := Sources[modName]

	if !ok {
		modsrc = LoadFile(*Borges + "/" + modName)
		Sources[modName] = modsrc
	}

	if modsrc == nil {
		Sources[modName] = &ModSrc{
			Src:      make(map[uint]string),
			Filename: modName,
		}
		return ""
	}

	srcLine, _ := modsrc.Src[offset]
	return srcLine
}
