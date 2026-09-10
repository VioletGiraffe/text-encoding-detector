TEMPLATE = subdirs

SUBDIRS += sub_detector
sub_detector.subdir = text-encoding-detector

build_analyzer{
	SUBDIRS += sub_analyzer
	sub_analyzer.subdir = text-analyzer
	sub_analyzer.depends = sub_detector
}

# No dependency on sub_detector: the tests compile the same sources under their own flags
build_tests{
	SUBDIRS += sub_tests
	sub_tests.subdir = tests
}
