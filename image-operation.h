#pragma once
#include "image.h"
#include <typeinfo>
#include "time.h"

/// Operation on an image
struct ImageOperation {
    virtual string name() const abstract;
    virtual int64 time() const abstract;
	virtual int type() const { return 3; }
	virtual buffer<ImageF> apply3(const ImageF& /*red*/, const ImageF& /*green*/, const ImageF& /*blue*/) const { error(type()); }
	virtual ImageF apply1(const ImageF& /*red*/, const ImageF& /*green*/, const ImageF& /*blue*/) const { error(type()); }
};

generic struct ImageOperationT : ImageOperation {
    string name() const override { static string name = ({ TextData s (str(typeid(T).name())); s.whileInteger(); s.identifier(); }); return name; }
    int64 time() const override { return parseDate(__DATE__ " " __TIME__)*1000000000l; }
};
