package videox

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// probeISOBMFF is a lean self-built reader for common MP4/M4V/MOV files.
// Covers width/height/duration/codec for playback UI when ffprobe is
// missing — not a libavformat replacement.
func probeISOBMFF(path string) (Info, error) {
	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".mp4", ".m4v", ".mov":
	default:
		return Info{}, fmt.Errorf("lean probe: unsupported extension %q", ext)
	}
	f, err := os.Open(path)
	if err != nil {
		return Info{}, err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return Info{}, err
	}

	var info Info
	var haveGeom bool
	err = walkBoxes(f, 0, st.Size(), func(typ string, payload []byte) error {
		switch typ {
		case "mvhd":
			if d, ok := parseMVHD(payload); ok && info.Duration == 0 {
				info.Duration = d
			}
		case "tkhd":
			if w, h, ok := parseTKHD(payload); ok && w > 0 && h > 0 && !haveGeom {
				info.Width, info.Height = w, h
				haveGeom = true
			}
		case "stsd":
			codec, w, h, ok := parseSTSD(payload)
			if ok {
				if codec != "" && info.Codec == "" {
					info.Codec = codec
				}
				if w > 0 && h > 0 {
					info.Width, info.Height = w, h
					haveGeom = true
				}
			}
		case "mdhd":
			if d, ok := parseMDHD(payload); ok && info.Duration == 0 {
				info.Duration = d
			}
		}
		return nil
	})
	if err != nil {
		return Info{}, err
	}
	if !haveGeom || info.Width <= 0 || info.Height <= 0 {
		return Info{}, fmt.Errorf("lean probe: no video geometry in %s", filepath.Base(path))
	}
	if info.Codec == "" {
		info.Codec = "h264"
	}
	if info.PixFmt == "" {
		info.PixFmt = "yuv420p"
	}
	return info, nil
}

func walkBoxes(ra io.ReaderAt, start, end int64, fn func(typ string, payload []byte) error) error {
	off := start
	for off+8 <= end {
		var hdr [8]byte
		if _, err := ra.ReadAt(hdr[:], off); err != nil {
			return nil
		}
		size := uint64(binary.BigEndian.Uint32(hdr[0:4]))
		typ := string(hdr[4:8])
		headerLen := int64(8)
		if size == 1 {
			var big [8]byte
			if _, err := ra.ReadAt(big[:], off+8); err != nil {
				return nil
			}
			size = binary.BigEndian.Uint64(big[:])
			headerLen = 16
		} else if size == 0 {
			size = uint64(end - off)
		}
		if size < uint64(headerLen) {
			return fmt.Errorf("lean probe: bad box size for %q", typ)
		}
		boxEnd := off + int64(size)
		if boxEnd > end || boxEnd <= off {
			return nil
		}
		switch typ {
		case "moov", "trak", "mdia", "minf", "stbl", "edts":
			if err := walkBoxes(ra, off+headerLen, boxEnd, fn); err != nil {
				return err
			}
		default:
			plen := int(size) - int(headerLen)
			if plen < 0 {
				return nil
			}
			if plen > 4<<20 { // 4 MiB cap per leaf box
				plen = 4 << 20
			}
			buf := make([]byte, plen)
			n, _ := ra.ReadAt(buf, off+headerLen)
			if err := fn(typ, buf[:n]); err != nil {
				return err
			}
		}
		off = boxEnd
	}
	return nil
}

func parseMVHD(b []byte) (time.Duration, bool) {
	if len(b) < 20 {
		return 0, false
	}
	version := b[0]
	if version == 1 {
		if len(b) < 32 {
			return 0, false
		}
		// version(1)+flags(3)+ctime(8)+mtime(8)=20, then timescale(4)+duration(8)
		timescale := binary.BigEndian.Uint32(b[20:24])
		duration := binary.BigEndian.Uint64(b[24:32])
		if timescale == 0 {
			return 0, false
		}
		return time.Duration(duration) * time.Second / time.Duration(timescale), true
	}
	// version 0: version+flags(4)+ctime(4)+mtime(4)=12, timescale(4), duration(4)
	if len(b) < 20 {
		return 0, false
	}
	timescale := binary.BigEndian.Uint32(b[12:16])
	duration := binary.BigEndian.Uint32(b[16:20])
	if timescale == 0 {
		return 0, false
	}
	return time.Duration(duration) * time.Second / time.Duration(timescale), true
}

func parseMDHD(b []byte) (time.Duration, bool) {
	return parseMVHD(b) // same versioned timescale/duration layout
}

func parseTKHD(b []byte) (w, h int, ok bool) {
	version := byte(0)
	if len(b) > 0 {
		version = b[0]
	}
	var off int
	if version == 1 {
		// version+flags(4)+ctime(8)+mtime(8)+trackID(4)+reserved(4)+duration(8)+… matrix …
		// width/height are 16.16 at end of fixed header: offset 96
		off = 96
	} else {
		off = 76
	}
	if len(b) < off+8 {
		return 0, 0, false
	}
	w = int(binary.BigEndian.Uint32(b[off:off+4]) >> 16)
	h = int(binary.BigEndian.Uint32(b[off+4:off+8]) >> 16)
	if w <= 0 || h <= 0 {
		return 0, 0, false
	}
	return w, h, true
}

func parseSTSD(b []byte) (codec string, w, h int, ok bool) {
	// version+flags(4) + entry_count(4) + sample entry
	if len(b) < 16 {
		return "", 0, 0, false
	}
	entryCount := binary.BigEndian.Uint32(b[4:8])
	if entryCount == 0 {
		return "", 0, 0, false
	}
	entry := b[8:]
	if len(entry) < 16 {
		return "", 0, 0, false
	}
	// entrySize(4) + type(4) + ...
	typ := string(entry[4:8])
	codec = mapSampleType(typ)
	// VisualSampleEntry: after type, 6 reserved + 2 data_reference_index = 8,
	// then 16 bytes pre_defined/reserved, then width(2)+height(2) at offset 32 from entry start.
	if len(entry) >= 36 {
		w = int(binary.BigEndian.Uint16(entry[32:34]))
		h = int(binary.BigEndian.Uint16(entry[34:36]))
	}
	return codec, w, h, codec != "" || (w > 0 && h > 0)
}

func mapSampleType(typ string) string {
	switch typ {
	case "avc1", "avc3":
		return "h264"
	case "hev1", "hvc1":
		return "hevc"
	case "mp4v":
		return "mpeg4"
	case "vp09":
		return "vp9"
	case "av01":
		return "av1"
	default:
		return strings.TrimRight(typ, "\x00")
	}
}
