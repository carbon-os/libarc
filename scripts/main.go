package main

import (
	"archive/zip"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// ProgressReader wraps an io.Reader to print a live CLI progress bar
type ProgressReader struct {
	io.Reader
	Total    int64
	Current  int64
	Start    time.Time
	LastTick time.Time
}

func (pr *ProgressReader) Read(p []byte) (int, error) {
	n, err := pr.Reader.Read(p)
	pr.Current += int64(n)

	// Throttle console updates to 100ms to prevent flickering, 
	// unless we've hit the exact total.
	if time.Since(pr.LastTick) > 100*time.Millisecond || pr.Current == pr.Total {
		pr.LastTick = time.Now()
		pr.printProgress()
	}
	return n, err
}

func (pr *ProgressReader) printProgress() {
	// Fallback if the server doesn't provide a Content-Length
	if pr.Total <= 0 {
		fmt.Printf("\r  Downloading... %.2f MB", float64(pr.Current)/1024/1024)
		return
	}

	pct := float64(pr.Current) / float64(pr.Total)
	width := 40
	completed := int(float64(width) * pct)

	bar := strings.Repeat("█", completed) + strings.Repeat("░", width-completed)
	elapsed := time.Since(pr.Start).Seconds()
	
	// Prevent divide-by-zero on extremely fast connections at 0.000s
	if elapsed == 0 {
		elapsed = 0.001
	}
	speed := (float64(pr.Current) / 1024 / 1024) / elapsed

	fmt.Printf("\r  [%s] %5.1f%% | %.1f/%.1f MB | %5.1f MB/s ",
		bar, pct*100,
		float64(pr.Current)/1024/1024, float64(pr.Total)/1024/1024,
		speed)
}

func main() {
	// Setup command-line arguments
	version := flag.String("version", "146.0.3856.97", "WebView2 Runtime version to download")
	dest := flag.String("dest", "", "Destination folder (defaults to ~/Downloads/webview2_runtime/<version>)")
	flag.Parse()

	// Default to the user's Downloads folder if no destination is provided
	if *dest == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			log.Fatalf("\nERROR: Failed to get user home directory: %v", err)
		}
		*dest = filepath.Join(home, "Downloads", "webview2_runtime", *version)
	}

	fmt.Println("==================================================")
	fmt.Printf(" Target Version : %s\n", *version)
	fmt.Printf(" Destination    : %s\n", *dest)
	fmt.Println("==================================================")

	// 1. Download the NuGet package
	url := fmt.Sprintf("https://www.nuget.org/api/v2/package/WebView2.Runtime.X64/%s", *version)
	fmt.Printf("\nConnecting to NuGet...\n")

	tmpFile, err := os.CreateTemp("", "webview2_*.nupkg")
	if err != nil {
		log.Fatalf("\nERROR: Failed to create temp file: %v", err)
	}
	defer os.Remove(tmpFile.Name())

	resp, err := http.Get(url)
	if err != nil {
		log.Fatalf("\nERROR: Download request failed: %v", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		log.Fatalf("\nERROR: Download failed with HTTP status: %s", resp.Status)
	}

	// Wrap the HTTP response body with our fancy progress reader
	progressBody := &ProgressReader{
		Reader: resp.Body,
		Total:  resp.ContentLength,
		Start:  time.Now(),
	}

	if _, err := io.Copy(tmpFile, progressBody); err != nil {
		log.Fatalf("\nERROR: Failed to write to temp file: %v", err)
	}
	fmt.Println() // Print a clean newline after the progress bar finishes

	// 2. Unpack the ZIP archive in-memory
	fmt.Printf("\nScanning archive for runtime binaries...\n")

	info, err := tmpFile.Stat()
	if err != nil {
		log.Fatalf("ERROR: Failed to stat temp file: %v", err)
	}

	zr, err := zip.NewReader(tmpFile, info.Size())
	if err != nil {
		log.Fatalf("ERROR: Failed to open zip reader: %v", err)
	}

	var baseDir string
	found := false
	for _, f := range zr.File {
		if strings.HasSuffix(strings.ToLower(f.Name), "msedgewebview2.exe") {
			baseDir = filepath.ToSlash(filepath.Dir(f.Name))
			found = true
			break
		}
	}

	if !found {
		log.Fatalf("ERROR: Could not find msedgewebview2.exe inside the package.")
	}

	if baseDir != "." && !strings.HasSuffix(baseDir, "/") {
		baseDir += "/"
	} else if baseDir == "." {
		baseDir = ""
	}

	fmt.Printf("  Found at: %s\n", baseDir)
	fmt.Printf("\nExtracting to destination...\n")

	if err := os.MkdirAll(*dest, 0755); err != nil {
		log.Fatalf("ERROR: Failed to create destination dir: %v", err)
	}

	// Filter files we actually want to extract
	var filesToExtract []*zip.File
	for _, f := range zr.File {
		if baseDir == "" || strings.HasPrefix(f.Name, baseDir) {
			filesToExtract = append(filesToExtract, f)
		}
	}

	totalFiles := len(filesToExtract)
	for i, f := range filesToExtract {
		// Live extraction progress counter
		fmt.Printf("\r  Extracting file %d of %d...", i+1, totalFiles)

		relPath := strings.TrimPrefix(f.Name, baseDir)
		if relPath == "" {
			continue
		}

		targetPath := filepath.Join(*dest, relPath)

		if f.FileInfo().IsDir() {
			os.MkdirAll(targetPath, 0755)
			continue
		}

		os.MkdirAll(filepath.Dir(targetPath), 0755)

		rc, err := f.Open()
		if err != nil {
			log.Fatalf("\nERROR: Failed to read file from zip: %v", err)
		}

		out, err := os.Create(targetPath)
		if err != nil {
			rc.Close()
			log.Fatalf("\nERROR: Failed to write extracted file: %v", err)
		}

		_, err = io.Copy(out, rc)
		out.Close()
		rc.Close()

		if err != nil {
			log.Fatalf("\nERROR: Failed to save extracted file: %v", err)
		}
	}
	fmt.Println() // Clean newline after extraction counter

	fmt.Printf("\n✓ Success! WebView2 runtime is ready at:\n  %s\n", *dest)
}