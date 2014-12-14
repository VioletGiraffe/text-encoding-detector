TEMPLATE = subdirs

SUBDIRS += sub_detector
sub_detector.subdir = text-encoding-detector

build_analyzer{
	SUBDIRS += sub_analyzer
	sub_analyzer.subdir = text-analyzer
	sub_analyzer.depends = sub_detector
}
