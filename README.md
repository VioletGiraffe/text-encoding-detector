Text encoding detector
======================

A Qt-based library for detecting the encoding of binary data assuming it is a text, and converting it to QString properly.
Languages supported so far:
* English
* Russian.

Adding support for new languages is extremely simple and fast, the appropriate tool is included in this repository.

### Usage

Decoding a memory buffer (`QByteArray`):
``` c++
QByteArray textData = getTextData();
const auto result = CTextEncodingDetector::decode(textData);
qDebug() << "Detected language:" << result.language;
qDebug() << "Detected encoding:" << result.encodingName;
qDebug() << "Decoded text:" << result.text;
```

Decoding data from a `QIODevice` (`QFile` for demonstration purposes here):
``` c++
QFile textFile("unknown_encoding.txt");
textFile.open(QFile::ReadOnly);
const auto result = CTextEncodingDetector::decode(textFile);
qDebug() << "Detected language:" << result.language;
qDebug() << "Detected encoding:" << result.encodingName;
qDebug() << "Decoded text:" << result.text;
```

Decoding data from a file given its path (`QString`) - same as the previous example, but shorter:
``` c++
const auto result = CTextEncodingDetector::decode("unknown_encoding.txt");
qDebug() << "Detected language:" << result.language;
qDebug() << "Detected encoding:" << result.encodingName;
qDebug() << "Decoded text:" << result.text;
```

### Suporting other languages

Build the text_analyzer console application from the text-analyzer folder of this repo. Run the application on a bunch of UTF-8 text files in the target language:

`text_analyzer <language name> <path to textfile 1> [path to textfile 2] ... [path to textfile N]`

The output will be `ctrigramfrequencytable_<Language name>.h` and `ctrigramfrequencytable_<Language name>.cpp` source files in the working directory, containing the declaration and definition of the `CTrigramFrequencyTable_<Language name>` class. Add it to your project, and then supply your own frequency tables to the encoding detector using the optional second parameter to `CTextEncodingDetector::decode`. Note that if you also want any of the default tables, you will have to also provide them manually:

``` c++
const auto result = CTextEncodingDetector::decode("unknown_encoding.txt", {
                    std::move(std::make_unique<CTrigramFrequencyTable_Spanish>),
                    std::move(std::make_unique<CTrigramFrequencyTable_German>),
                    std::move(std::make_unique<CTrigramFrequencyTable_English>),
                    std::move(std::make_unique<CTrigramFrequencyTable_Russian>)
                    });
qDebug() << "Detected language:" << result.language;
qDebug() << "Detected encoding:" << result.encodingName;
qDebug() << "Decoded text:" << result.text;
```

### Building

* A compiler with C++ 11 support is required.
* Windows: you can build using either Qt Creator or Visual Studio for IDE. Visual Studio 2013 or newer is required - v120 toolset or newer. Run `qmake -tp vc -r` to generate the solution for Visual Studio. I have not tried building with MinGW, but it should work as long as you enable C++ 11 support.
* Linux: open the project file in Qt Creator and build it.
* Mac OS X: You can use either Qt Creator (simply open the project in it) or Xcode (run `qmake -r -spec macx-xcode` and open the Xcode project that has been generated).
