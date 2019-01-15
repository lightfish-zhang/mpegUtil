package main

import (
	"fmt"
	"io/ioutil"
	"os"

	mpegUtil "github.com/lightfish-zhang/mpegUtil/c"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Printf("Usage: %s <input file> <output file>\n", os.Args[0])
		os.Exit(1)
	}
	inFile, err := os.Open(os.Args[1])
	if err != nil {
		fmt.Printf("open file fail, path=%v, err=%v", os.Args[1], err)
		os.Exit(1)
	}
	outFile, err := os.Create(os.Args[2])
	if err != nil {
		fmt.Printf("create file fail, path=%v, err=%v", os.Args[2], err)
		os.Exit(1)
	}
	input, err := ioutil.ReadAll(inFile)
	if err != nil {
		fmt.Printf("read file fail, err=%v", err)
		os.Exit(1)
	}

	err, output := mpegUtil.GenGif(5, 90, input)
	if err != nil {
		fmt.Printf("generate gif fail, err=%v", err)
		os.Exit(1)
	}
	_, err = outFile.Write(output)
	if err != nil {
		fmt.Printf("write file fail, err=%v", err)
		os.Exit(1)
	}
}
