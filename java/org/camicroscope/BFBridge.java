package org.camicroscope;

import loci.formats.IFormatReader;
// https://downloads.openmicroscopy.org/bio-formats/7.0.0/api/loci/formats/IFormatReader.html etc. for documentation
import loci.formats.ImageReader;
import loci.formats.ReaderWrapper;
import loci.formats.FormatTools;
// import loci.formats.MetadataTools;
// import loci.formats.services.OMEXMLServiceImpl;
import loci.formats.ome.OMEXMLMetadataImpl;
import loci.formats.Memoizer;
import loci.formats.ome.OMEXMLMetadata;
import loci.formats.services.JPEGTurboServiceImpl;
import ome.units.UNITS;

// import loci.formats.tools.ImageConverter;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.Charset;
import java.nio.file.Files;

import javax.imageio.ImageIO;
import javax.imageio.stream.ImageInputStream;
import javax.imageio.stream.MemoryCacheImageInputStream;

// Useful Docs:
// https://bio-formats.readthedocs.io/en/v6.14.0/developers/file-reader.html#reading-files

public class BFBridge {
    // To allow both cached and noncached setup with one type:
    // ImageReader is our noncached reader but it doesn't
    // implement ReaderWrapper so make a wrapper to make substitution
    // possible with Memoizer
    private final class BFReaderWrapper extends ReaderWrapper {
        BFReaderWrapper(IFormatReader r) {
            super(r);
        }
    }

    // BioFormats doesn't give us control over thumbnail sizes
    // unless we define a wrapper for FormatTools.openThumbBytes
    // https://github.com/ome/bioformats/blob/9cb6cfaaa5361bcc4ed9f9841f2a4caa29aad6c7/components/formats-api/src/loci/formats/FormatTools.java#L1287
    private final class BFThumbnailWrapper extends ReaderWrapper {
        // exact sizes
        private int thumbX = 256;
        private int thumbY = 256;

        // For our use
        void setThumbSizeX(int x) {
            thumbX = x;
        }

        void setThumbSizeY(int y) {
            thumbY = y;
        }

        // For FormatTools.openThumbBytes
        @Override
        public int getThumbSizeX() {
            return thumbX;
        }

        @Override
        public int getThumbSizeY() {
            return thumbY;
        }

        BFThumbnailWrapper(IFormatReader r) {
            super(r);
        }
    }

    private final BFThumbnailWrapper readerWithThumbnailSizes;
    private final ReaderWrapper reader;

    // Our uncaching internal reader. ImageReader and ReaderWrapper
    // both implement IFormatReader but if you need an ImageReader-only
    // method, access this. (You could also do (inefficiently)
    // .getReader() on "reader" and cast it to ImageReader
    // since that's what we use)
    private final ImageReader nonCachingReader = new ImageReader();

    // As a summary, nonCachingReader is the reader
    // which is wrapped by BFReaderWrapper or Memoizer
    // which is sometimes wrapped by readerWithThumbnailSizes
    // For performance, this library calls the readerWithThumbnailSizes
    // wrapper only when it needs thumbnail.
    // Please note that reinstantiating nonCachingReader requires
    // reinstantiating "ReaderWrapper reader" (BFReaderWrapper or Memoizer).
    // And reinstantiating the latter requires reinstantiating
    // the readerWithThumbnailSizes

    private final OMEXMLMetadataImpl metadata = new OMEXMLMetadataImpl();

    // javac -DBFBridge.cachedir=/tmp/cachedir for faster opening of files
    private static final File cachedir;

    // Initialize cache
    static {
        String cachepath = System.getProperty("BFBridge.cachedir");
        if (cachepath == null) {
            cachepath = System.getenv("BFBRIDGE_CACHEDIR");
        }
        System.out.println("Trying BFBridge cache directory: " + cachepath);

        File _cachedir = null;
        if (cachepath == null || cachepath.equals("")) {
            System.out.println("Skipping BFBridge cache");
        } else {
            _cachedir = new File(cachepath);
        }
        if (_cachedir != null && !_cachedir.exists()) {
            System.out.println("BFBridge cache directory does not exist, skipping!");
            _cachedir = null;
        }
        if (_cachedir != null && !_cachedir.isDirectory()) {
            System.out.println("BFBridge cache directory is not a directory, skipping!");
            _cachedir = null;
        }
        if (_cachedir != null && !_cachedir.canRead()) {
            System.out.println("cannot read from the BFBridge cache directory, skipping!");
            _cachedir = null;
        }
        if (_cachedir != null && !_cachedir.canWrite()) {
            System.out.println("cannot write to the BFBridge cache directory, skipping!");
            _cachedir = null;
        }
        if (_cachedir != null) {
            System.out.println("activating BFBridge cache");
        }
        cachedir = _cachedir;
    }

    // Initialize our instance reader
    {
        if (cachedir == null) {
            reader = new BFReaderWrapper(nonCachingReader);
        } else {
            reader = new Memoizer(nonCachingReader, cachedir);
        }

        // Use the easier resolution API
        reader.setFlattenedResolutions(false);
        reader.setMetadataStore(metadata);
        // Save format-specific metadata as well?
        // metadata.setOriginalMetadataPopulated(true);

        readerWithThumbnailSizes = new BFThumbnailWrapper(reader);
    }

    // Use a shared buffer for two-way communication.
    // pass bytes written as params/return value.
    // when an error is written, update "lastErrorBytes".
    // Please remember to null-terminate.
    // Using a shared buffer is not supported
    // for every JVM JNI/GraalVM, in which
    // case the wrapper will display an error.
    // Make sure to
    // communicationBuffer.rewind() every time before reading/writing
    // from Java.

    private static final Charset charset = Charset.forName("UTF-8");
    private ByteBuffer communicationBuffer = null;
    private int lastErrorBytes = 0;

    void BFSetCommunicationBuffer(ByteBuffer b) {
        communicationBuffer = b;
        communicationBuffer.order(ByteOrder.LITTLE_ENDIAN);
    }

    int BFGetErrorLength() {
        return lastErrorBytes;
    }

    // Please note: this closes the previous file
    // Input Parameter: first filenameLength bytes of communicationBuffer.
    int BFIsCompatible(int filenameLength) {
        try {
            byte[] filename = new byte[filenameLength];
            communicationBuffer.rewind().get(filename);
            close();

            // If we didn't have this line, I would change
            // "private ImageReader reader" to
            // "private IFormatReader reader"
            // and we would access only through the WrappedReader/Memoizer
            // and not the ImageReader
            return nonCachingReader.getReader(new String(filename)) != null ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        } finally {
            close();
        }
    }

    int BFIsAnyFileOpen() {
        try {
            return reader.getCurrentFile() != null ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Input Parameter: first filenameLength bytes of communicationBuffer
    int BFOpen(int filenameLength) {
        try {
            byte[] filename = new byte[filenameLength];
            communicationBuffer.rewind().get(filename);
            close();
            reader.setId(new String(filename));
            return 1;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            close();
            return -1;
        }
    }

    // writes to communicationBuffer and returns the number of bytes written
    int BFGetFormat() {
        try {
            byte[] formatBytes = reader.getFormat().getBytes(charset);
            communicationBuffer.rewind().put(formatBytes);
            return formatBytes.length;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // If expected to be the single file; always true for single-file formats
    // Input Parameter: first filenameLength bytes of communicationBuffer.
    int BFIsSingleFile(int filenameLength) {
        try {
            byte[] filename = new byte[filenameLength];
            communicationBuffer.rewind().get(filename);

            close();
            return reader.isSingleFile(new String(filename)) ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        } finally {
            close();
        }
    }

    // writes to communicationBuffer and returns the number of bytes written
    // 0 if no file was opened
    int BFGetCurrentFile() {
        try {
            String file = reader.getCurrentFile();
            if (file == null) {
                return 0;
            } else {
                byte[] characters = file.getBytes(charset);
                communicationBuffer.rewind().put(characters);
                return characters.length;
            }
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Lists null-separated filenames. Returns bytes written including the last null
    int BFGetUsedFiles() {
        try {
            communicationBuffer.rewind();
            String[] files = reader.getUsedFiles();
            int charI = 0;
            for (String file : files) {
                byte[] characters = file.getBytes(charset);
                if (characters.length + 2 > communicationBuffer.capacity()) {
                    saveError("Too long");
                    return -2;
                }
                communicationBuffer.put(characters);
                communicationBuffer.put((byte) 0);
                charI += characters.length + 1;
            }
            return charI;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    int BFClose() {
        try {
            reader.close();
            return 1;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Series are independent images
    // Resolutions are layers of an image
    int BFGetSeriesCount() {
        try {
            return reader.getSeriesCount();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    int BFSetCurrentSeries(int no) {
        try {
            reader.setSeries(no);
            return 1;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Resolution (layer) count for current series (image)
    int BFGetResolutionCount() {
        try {
            return reader.getResolutionCount();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // resIndex from 0 to resolution count minus one
    int BFSetCurrentResolution(int resIndex) {
        try {
            reader.setResolution(resIndex);
            return 1;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // The following five functions are
    // true for a series, not for a file

    // Width of current resolution in current series
    int BFGetSizeX() {
        try {
            // For current resolution
            return reader.getSizeX();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Height
    int BFGetSizeY() {
        try {
            return reader.getSizeY();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Colors
    int BFGetSizeC() {
        try {
            return reader.getSizeC();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Depth
    int BFGetSizeZ() {
        try {
            return reader.getSizeZ();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Time
    int BFGetSizeT() {
        try {
            return reader.getSizeT();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // "R+G+B" counts as one
    // https://downloads.openmicroscopy.org/bio-formats/latest/api/loci/formats/IFormatReader.html#getEffectiveSizeC--
    int BFGetEffectiveSizeC() {
        try {
            return reader.getEffectiveSizeC();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // number of images in current series
    int BFGetImageCount() {
        try {
            return reader.getImageCount();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // writes to communicationBuffer and returns the number of bytes written (5 bytes)
    int BFGetDimensionOrder() {
        try {
            byte[] strBytes = reader.getDimensionOrder().getBytes(charset);
            communicationBuffer.rewind().put(strBytes);
            return strBytes.length;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    int BFIsOrderCertain() {
        try {
            return reader.isOrderCertain() ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    int BFGetOptimalTileWidth() {
        try {
            return reader.getOptimalTileWidth();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    int BFGetOptimalTileHeight() {
        try {
            return reader.getOptimalTileHeight();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Pixel type, endianness, channel count,
    // and especially endianness may vary
    // among resolutions

    // Internal BioFormats pixel type
    int BFGetPixelType() {
        try {
            // https://github.com/ome/bioformats/blob/4a08bfd5334323e99ad57de00e41cd15706164eb/components/formats-api/src/loci/formats/FormatReader.java#L735
            // https://github.com/ome/bioformats/blob/9cb6cfaaa5361bcc4ed9f9841f2a4caa29aad6c7/components/formats-api/src/loci/formats/FormatTools.java#L835
            // https://github.com/ome/bioformats/blob/9cb6cfaaa5361bcc4ed9f9841f2a4caa29aad6c7/components/formats-api/src/loci/formats/FormatTools.java#L1507
            return reader.getPixelType();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // openBytes documentation makes it clear that
    // https://downloads.openmicroscopy.org/bio-formats/latest/api/loci/formats/ImageReader.html#openBytes-int-byte:A-
    // this function gives the number of
    // bits per pixel per channel
    int BFGetBitsPerPixel() {
        // https://downloads.openmicroscopy.org/bio-formats/latest/api/loci/formats/IFormatReader.html#getPixelType--
        // https://github.com/ome/bioformats/blob/9cb6cfaaa5361bcc4ed9f9841f2a4caa29aad6c7/components/formats-api/src/loci/formats/FormatTools.java#L96
        try {
            return reader.getBitsPerPixel();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // gives bytes per pixel in a channel
    int BFGetBytesPerPixel() {
        try {
            return FormatTools.getBytesPerPixel(reader.getPixelType());
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // (Almost?) always equal to sizeC. Can be 3, can be 4.
    int BFGetRGBChannelCount() {
        try {
            return reader.getRGBChannelCount();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // if a single openBytes call returns an array describing multiple colors
    int BFIsRGB() {
        try {
            return reader.isRGB() ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // order of colors in openBytes returned array
    int BFIsInterleaved() {
        try {
            return reader.isInterleaved() ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }
    
    int BFIsLittleEndian() {
        try {
            return reader.isLittleEndian() ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // https://downloads.openmicroscopy.org/bio-formats/7.0.0/api/loci/formats/IFormatReader.html#isFalseColor--
    // when we have 8 or 16 bits per channel, these might be signifying
    // indices in color profile.
    // isindexed false, isfalsecolor false -> no table
    // isindexed true, isfalsecolor false -> table must be read
    // isindexed true, isfalsecolor true -> table can be read, not obligatorily
    int BFIsFalseColor() {
        // note: lookup tables need to be cached by us
        // as some readers such as
        // https://github.com/ome/bioformats/blob/65db5eb2bb866ebde42c8d6e2611818612432828/components/formats-bsd/src/loci/formats/in/OMETiffReader.java#L310
        // do not serve from cache
        try {
            return reader.isFalseColor() ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    int BFIsIndexedColor() {
        try {
            return reader.isIndexed() ? 1 : 0;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Serializes a 2D table with 2nd dimension length 256
    // Returns number of bytes written
    int BFGet8BitLookupTable() {
        try {
            byte[][] table = reader.get8BitLookupTable();
            int len = table.length;
            int sublen = table[0].length;
            if (sublen != 256) {
                saveError("BFGet8BitLookupTable expected 256 rowlength");
                return -2;
            }
            byte[] table1D = new byte[len * sublen];
            for (int i = 0; i < len; i++) {
                for (int j = 0; j < sublen; j++) {
                    table1D[i * sublen + j] = table[i][j];
                }
            }
            communicationBuffer.rewind().put(table1D);
            return len * sublen;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // Returns number of bytes written
    // With a JNI communicationBuffer of type ByteBuffer,
    // little endian
    int BFGet16BitLookupTable() {
        try {
            short[][] table = reader.get16BitLookupTable();
            int len = table.length;
            int sublen = table[0].length;
            if (sublen != 65536) {
                saveError("BFGet16BitLookupTable expected 65536 rowlength");
                return -2;
            }
            short[] table1D = new short[len * sublen];
            for (int i = 0; i < len; i++) {
                for (int j = 0; j < sublen; j++) {
                    table1D[i * sublen + j] = table[i][j];
                }
            }
            communicationBuffer.rewind();
            for (int i = 0; i < table1D.length; i++) {
                communicationBuffer.putShort(table1D[i]);
            }
            return 2 * len * sublen;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // plane is 0, default
    // writes to communicationBuffer and returns the number of bytes written
    int BFOpenBytes(int plane, int x, int y, int w, int h) {
        try {
            // https://github.com/ome/bioformats/issues/4058 means that
            // openBytes wasn't designed to copy to a preallocated byte array
            // unless it had the exact size and not greater
            System.err.println("xywh" + x + " " + y + " " + w + " " + h);
            byte[] bytes = reader.openBytes(0, x, y, w, h);
            communicationBuffer.rewind().put(bytes);
            return bytes.length;
        } catch (Exception e) {
            // Was it because of exceeding buffer?
            // https://github.com/ome/bioformats/blob/4a08bfd5334323e99ad57de00e41cd15706164eb/components/formats-api/src/loci/formats/FormatReader.java#L906
            // https://downloads.openmicroscopy.org/bio-formats/6.13.0/api/loci/formats/ImageReader.html#openBytes-int-byte:A-
            try {
                int size = w * h * FormatTools.getBytesPerPixel(reader.getPixelType()) * reader.getRGBChannelCount();
                if (size > communicationBuffer.capacity()) {
                    saveError("Requested tile too big; must be at most " + communicationBuffer.capacity()
                            + " bytes but wanted " + size);
                    return -2;
                }
            } catch (Exception e2) {
            } finally {
                saveError(getStackTrace(e));
                return -1;
            }
        }
    }

   // warning: changes the current resolution level
    // takes exact width and height.
    // the caller should ensure the correct aspect ratio.
    // writes to communicationBuffer and returns the number of bytes written
    // prepares 3 channel or 4 channel, same sample format
    // and bitlength (but made unsigned if was int8 or int16 or int32)
    int BFOpenThumbBytes(int plane, int width, int height) {
        try {
            /*
             * float yOverX = reader.getSizeY() / reader.getSizeX();
             * float xOverY = 1/yToX;
             * int width = Math.min(maxWidth, maxHeight * xOverY);
             * int height = Math.min(maxHeight, maxWidth * yOverX);
             * Also potentially a check so that if width is greater than getSizeX
             * or likewise for height, use image resolution.
             */

            readerWithThumbnailSizes.setThumbSizeX(width);
            readerWithThumbnailSizes.setThumbSizeY(height);

            int resCount = reader.getResolutionCount();
            reader.setResolution(resCount - 1);

            // Using class's openThumbBytes
            // instead of FormatTools.openThumbBytes 
            // might break our custom thumbnail sizes?
            byte[] bytes = FormatTools.openThumbBytes(readerWithThumbnailSizes, plane);
            communicationBuffer.rewind().put(bytes);
            return bytes.length;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    // An alternative to openBytes is openPlane
    // https://downloads.openmicroscopy.org/bio-formats/latest/api/loci/formats/IFormatReader.html#openPlane-int-int-int-int-int-
    // some types are
    // https://github.com/search?q=repo%3Aome%2Fbioformats+getNativeDataType&type=code

    // series is 0, default
    // https://bio-formats.readthedocs.io/en/latest/metadata-summary.html
    // 0 if not defined, -1 for error
    double BFGetMPPX(int series) {
        try {
            // Maybe consider modifying to handle multiple series
            var size = metadata.getPixelsPhysicalSizeX(series);
            if (size == null) {
                return 0d;
            }
            return size.value(UNITS.MICROMETER).doubleValue();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1d;
        }

    }

    double BFGetMPPY(int series) {
        try {
            var size = metadata.getPixelsPhysicalSizeY(series);
            if (size == null) {
                return 0d;
            }
            return size.value(UNITS.MICROMETER).doubleValue();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1d;
        }
    }

    double BFGetMPPZ(int series) {
        try {
            var size = metadata.getPixelsPhysicalSizeZ(series);
            if (size == null) {
                return 0d;
            }
            return size.value(UNITS.MICROMETER).doubleValue();
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1d;
        }
    }

    int BFDumpOMEXMLMetadata() {
        try {
            String metadataString = metadata.dumpXML();
            byte[] bytes = metadataString.getBytes(charset);
            if (bytes.length > communicationBuffer.capacity()) {
                saveError("BFDumpOMEXMLMetadata: needed buffer of length at least " + bytes.length + " but current buffer is of length " + communicationBuffer.capacity());
                return -2;
            }
            communicationBuffer.rewind().put(bytes);
            return bytes.length;
        } catch (Exception e) {
            saveError(getStackTrace(e));
            return -1;
        }
    }

    private static String getStackTrace(Throwable t) {
        StringWriter sw = new StringWriter();
        t.printStackTrace(new PrintWriter(sw));
        return t.toString() + "\n" + sw.toString();
    }

    private void close() {
        try {
            reader.close();
        } catch (Exception e) {

        }
    }

    private void saveError(String s) {
        byte[] errorBytes = s.getBytes(charset);
        int bytes_len = errorBytes.length;
        // -1 to account for the null byte for security
        bytes_len = Math.min(bytes_len, Math.max(communicationBuffer.capacity() - 1, 0));
        // Trim error message
        communicationBuffer.rewind().put(errorBytes, 0, bytes_len);
        lastErrorBytes = bytes_len;
    }
}
