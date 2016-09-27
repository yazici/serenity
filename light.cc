#include "png.h"
#include "interface.h"
#include "window.h"

inline String str(range r) { return str(r.start, r.stop); } // -> string.h

struct SliderSurface : virtual Widget {
    function<void(int3)> valueChanged;
    int3 minimum, maximum;
    int3 value;
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override;
};
bool SliderSurface::mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) {
    int3 value = this->value;
    if(event == Press && (button == WheelUp || button == WheelDown)) {
     value.z = (this->value.z+maximum.z+(button==WheelUp?1:-1))%(maximum.z+1);
    }
    if((event == Motion || event==Press) && button==LeftButton) {
        value.xy() = minimum.xy()+int2(cursor)*(maximum.xy()-minimum.xy())/int2(size);
    }
    if(value != this->value) {
        this->value = value;
        if(valueChanged) {
            valueChanged(value);
            return true;
        }
    }
    return false;
}

struct SliderView : SliderSurface, ImageView { ~SliderView(); };
SliderView::~SliderView() {}

static struct Light {
    Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};
    buffer<String> inputs = currentWorkingDirectory().list(Folders);

    string name;
    array<Map> maps;
    ImageT<Image> images;
    SliderView view;
    unique<Window> window = nullptr;

    Light() {
        assert_(inputs);
        load(inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
    }
    void load(string name) {
        view.image = Image();
        array<Image>(::move(images)).clear(); // Proper destruction in case heap allocated
        maps.clear();
        this->name = name;
        Folder input (name);
        Folder tmp (name, this->tmp, true);

        range xRange {0}, yRange {0};
        for(string name: input.list(Files)) {
            TextData s (name);
            s.until('_');
            int y = s.integer();
            s.match('_');
            int x = s.integer();

            xRange.start = ::min(xRange.start, x);
            xRange.stop = ::max(xRange.stop, x+1);

            yRange.start = ::min(yRange.start, y);
            yRange.stop = ::max(yRange.stop, y+1);
        }

        images = ImageT<Image>(uint(xRange.size()), uint(yRange.size()));
        images.clear(Image());

        for(string name: input.list(Files)) {
            TextData s (name);
            s.until('_');
            uint y = uint(s.integer(false));
            s.match('_');
            uint x = uint(s.integer(false));

            for(string mapName: tmp.list(Files)) {
                if(find(mapName, name)) {
                    TextData s (mapName);
                    s.until(".png.");
                    uint w = uint(s.integer(false));
                    s.match('x');
                    uint h = uint(s.integer(false));
                    images(x, y) = Image(cast<byte4>(unsafeRef(maps.append(mapName, tmp))), uint2(w, h));
                    goto continue2_;
                }
            }
            {
                log(name);
                Image image = decodeImage(Map(name, input));
                assert_(image.stride == image.size.x);
                String mapName = name+'.'+strx(image.size);
                writeFile(mapName, cast<byte>(image), tmp);
                images(x, y) = Image(cast<byte4>(unsafeRef(maps.append(mapName, tmp))), image.size);
            }
            continue2_:;
        }
        view.minimum = int3(xRange.start, yRange.start, 0);
        view.maximum = int3(xRange.stop-1, yRange.stop-1, inputs.size-1);
        view.valueChanged = [this](int3 value) {
            if(inputs[size_t(value.z)] != this->name) load(inputs[size_t(value.z)]);
            view.image = unsafeShare(images(images.size.x-1-uint(value.x), uint(value.y)));
        };
        view.valueChanged(view.value = int3((xRange.start+xRange.stop)/2, (yRange.start+yRange.stop)/2, inputs.indexOf(name)));
        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
} app;
