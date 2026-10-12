package main

import (
	"encoding/base64"
	"os"
)

func tempFile(pattern string) (string, error) {
	f, err := os.CreateTemp("", pattern)
	if err != nil {
		return "", err
	}
	path := f.Name()
	f.Close()
	return path, nil
}

func removeFile(path string) {
	os.Remove(path)
}

func fileToBase64(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(data), nil
}
