#pragma once
#include "source.h"
#include "serialization.h"
#include "exif.h"
#include "jpeg.h"

/// Cached collection of images backed by a source folder
struct ImageFolder : ImageSource, ImageRGBSource, PropertySource, map<String, map<String, String>> {
	Folder source;
    int2 maximumImageSize = 0;

	ImageFolder(const Folder& source, function<bool(const String& name, const map<String, String>& properties)> predicate={});

	String path() const override { return source.name(); }
	String name() const override { return String(section(source.name(),'/',-2,-1)); }
	size_t count(size_t) override { return map::size(); }
	size_t count() const { return map::size(); }
	size_t outputs() const override { return 3; }
    int2 maximumSize() const override { return maximumImageSize; }
	String elementName(size_t index) const override { assert_(index<count()); return copy(keys[index]); }
	int64 time(size_t index) override { return File(values[index].at("Path"_), source).modifiedTime(); }
	string at(size_t index) const override { return values[index].at("Date"); }
	int2 size(size_t index, int2 hint = 0) const override {
		int2 size = parse<int2>(values[index].at("Size"_));
		int2 resize = size; // Defaults to original size
		if(hint.x && hint.x < size.x) { assert_(size.x/hint.x, size, hint); resize = min(resize, size/(size.x/hint.x)); } // Fits width (Integer box resample)
		if(hint.y && hint.y < size.y) { assert_(size.y/hint.y); resize = min(resize, size/(size.y/hint.y)); } // Fits height (Integer box resample)
		assert_(size && resize.x/size.x == resize.y/size.y /*&& size.x/resize.x == size.y/resize.y, resize, size, vec2(size)/vec2(resize)*/);
		return resize;
	}

    /// Converts encoded sRGB images to raw (mmap'able) sRGB images
	SourceImageRGB image(size_t index, string parameters = "");

    /// Resizes sRGB images
    /// \note Resizing after linear float conversion would be more accurate but less efficient
	SourceImageRGB image(size_t index, int2 size, string parameters = "") override;

    /// Converts sRGB images to linear float images
	SourceImage image(size_t index, size_t componentIndex, int2 size, string parameters = "") override;
};
