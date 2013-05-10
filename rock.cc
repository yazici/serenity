#include "process.h"
#include "time.h"
#include "tiff.h"
#include "window.h"
#include "display.h"
#include "sample.h"
#include "render.h"
#include "operation.h"
#include "validation.h"
#include "smooth.h"
#include "threshold.h"
#include "distance.h"
#include "rasterize.h"
#include "maximum.h"

Operation Source("source"_,2); // Loads from original image slices
Operation ShiftRight ("source"_,"shift"_,2); // Shifts data to avoid overflows
Operation SmoothX("shift"_,"smoothx"_,2); // Denoises data and filters small pores by averaging samples in a window (X pass)
Operation SmoothY("smoothx"_,"smoothy"_,2);  // Y pass
Operation SmoothZ("smoothy"_,"smooth"_,2); // Z pass
Operation Threshold("smooth"_,"pore"_,4, "rock"_,4); // Segments in rock vs pore space by comparing to a fixed threshold
#if 1
Operation DistanceX("pore"_,"distancex"_,4); // Computes distance field to nearest rock (X pass)
Operation DistanceY("distancex"_,"distancey"_,4); // Y pass
Operation DistanceZ("distancey"_,"distance"_,4); // Z pass

Operation Rasterize("distance"_,"maximum"_,2); // Rasterizes each distance field voxel as a ball (with maximum blending)

Operation Tile("distance"_,"tiled_distance"_,2); // Search nearest local maximum of distance field (i.e maximum enclosing sphere)
Operation Maximum("tiled_distance"_,"walk"_,2); // Search nearest local maximum of distance field (i.e maximum enclosing sphere)

#else
Operation FeatureTransformX("threshold"_,"positionx"_,2); // Computes position of nearest rock wall (X pass)
Operation FeatureTransformY("positionx"_,"positiony"_,2); // Y pass
Operation FeatureTransformZ("positiony"_,"positionz"_,2); // Z pass
Operation Skeleton("positionx"_,"positiony"_,"positionz"_,"skeleton"_,2); // Computes integer medial axis skeleton
Operation Rasterize("skeleton"_,"maximum"_,2); // Rasterizes each distance field voxel as a ball (with maximum blending)
#endif

Operation Colorize("pore"_,"source"_,"colorize"_,3); // Maps intensity to either red or green channel depending on binary classification

Operation EmptyX("rock"_,"emptyx"_,4); // Computes distance field to nearest pore for empty space skipping (X pass)
Operation EmptyY("emptyx"_,"emptyy"_,4); // Y pass
Operation EmptyZ("emptyy"_,"emptyz"_,4); // Z pass
Operation RenderEmpty("emptyz"_,"empty"_,1); // Square roots and tiles distance field before using it for empty space skiping during rendering
Operation RenderDensity("distance"_,"density"_,1); // Square roots and normalizes distance to use as density values (for opacity and gradient)
Operation RenderIntensity("maximum"_,"intensity"_,1); // Square roots and normalizes maximum to use as intensity values (for color intensity)

Operation ASCII("maximum"_,"ascii"_,20); // Converts to ASCII (one voxel per line, explicit coordinates)

struct VolumeData {
    VolumeData(const ref<byte>& name):name(name){}
    string name;
    Map map;
    Volume volume;
    //uint referenceCount=0; //TODO: Counts how many operations need this volume (allows memory to be recycled when no operations need the data anymore)
};
template<> string str(const VolumeData& a) { return copy(a.name); }
bool operator ==(const VolumeData& a, const ref<byte>& name) { return a.name == name; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<ref<byte>>& arguments) {
        // Parses command line arguments
        ref<byte> target; array<ref<byte>> force;
        for(const ref<byte>& argument: arguments) {
            if(argument.contains('=')) { this->arguments.insert(section(argument,'=',0,1), section(argument,'=',1,-1)); continue; } // Stores generic argument to be parsed in relevant operation
            if(operationForOutput(argument)) { // Parses target (or intermediate data to be removed)
                force << argument;
                if(!target) target=argument;
                continue;
            }
            if(!source && argument=="balls"_) { source=argument, name=argument; continue; }
            if(existsFolder(argument)) {
                if(!source) { source=argument; name=source.contains('/')?section(source,'/',-2,-1):source; continue; }
                if(!result) { result=argument; resultFolder = argument; continue; }
            }
            if(!result) { result=argument; resultFolder = section(argument,'/',0,-2); continue; }
            error("Invalid argument"_, argument);
        }
        if(!result) result = "ptmp"_;
        if(!target) target=operations.last()->name;
        if(target!="ascii") {
            if(this->arguments.contains("selection"_)) selection = split(this->arguments.at("selection"_),',');
            if(!selection) selection<<"source"_<<"colorize"_<<"distance"_;
            if(!selection.contains(target)) selection<<target;
        }
        if(target=="intensity"_) renderVolume=true;
        cylinder = arguments.contains("cylinder"_) || (existsFolder(source) && !arguments.contains("cube"_));
        assert_(name);

        for(const string& path: memoryFolder.list(Files)) { // Maps intermediate data from any previous run
            if(!startsWith(path, name)) continue;
            VolumeData data = section(path,'.',-3,-2);
            bool remove = !operationForOutput(data.name) || volumes.contains(data.name);
            for(ref<byte> name: force) if(operationForOutput(name)->outputs.contains(data.name)) remove=true;
            if(remove) { ::remove(path, memoryFolder); continue; } // Removes invalid, multiple or to be removed data
            parseVolumeFormat(data.volume, path);
            File file = File(path, memoryFolder, ReadWrite);
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            data.volume.data = buffer<byte>(data.map);
            data.volume.sampleSize = data.volume.data.size / data.volume.size();
            assert(data.volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((data.volume.maximum+1))))) / 8); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
            volumes << move(data);
        }

        // Executes all operations
        current = getVolume(target);

        if(target=="ascii"_) { // Writes result to disk
            Time time;
            string volumeName = name+"."_+current->name+"."_+volumeFormat(current->volume);
            if(existsFolder(result)) writeFile(volumeName, current->volume.data, resultFolder), log(result+"/"_+volumeName, time);
            else writeFile(result, current->volume.data, root()), log(result, time);
            if(selection) current = getVolume(selection.last());
            else { exit(); return; }
        }
        // Displays result
        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        updateView();
        window.show();
    }
    ~Rock() {
        for(const string& path: memoryFolder.list(Files)) { // Cleanups intermediate data
            if(!startsWith(path, name)) continue;
            ref<byte> name = section(path,'.',-3,-2);
            if(!operationForOutput(name) || trash.contains(name)) { ::remove(path, memoryFolder); continue; } // Removes invalid or trashed data
        }
    }

    /// Computes target volume
    const VolumeData* getVolume(const ref<byte>& targetName) {
        for(const VolumeData& data: volumes) if(data.name == targetName) return &data;
        const Operation& operation = *operationForOutput(targetName);
        assert_(&operation, targetName);
        array<const VolumeData*> inputs;
        for(const ref<byte>& input: operation.inputs) inputs << getVolume( input );

        array<VolumeData*> outputs;
        for(const Operation::Output& output: operation.outputs) {
            VolumeData data = output.name;
            Volume& volume = data.volume;
            if(!operation.inputs) { // Original source slices format
                if(name == "balls"_) {
                    volume.x = 512, volume.y = 512, volume.z = 512;
                } else {
                    Folder folder(source);
                    array<string> slices = folder.list(Files);
                    Map file (slices.first(), folder);
                    const Tiff16 image (file);
                    volume.x = image.width, volume.y = image.height, volume.z = slices.size;
                    maxX = volume.x, maxY = volume.y, maxZ = volume.z;
                    if(arguments.contains("cylinder"_)) {
                        auto coordinates = toIntegers(arguments.at("cylinder"_));
                        int x=coordinates[0], y=coordinates[1], r=coordinates[2]; minZ=coordinates[3], maxZ=coordinates[4];
                        minX=x-r, minY=y-r, maxX=x+r, maxY=y+r;
                    }
                    if(arguments.contains("cube"_)) {
                        auto coordinates = toIntegers(arguments.at("cube"_));
                        minX=coordinates[0], minY=coordinates[1], minZ=coordinates[2], maxX=coordinates[3], maxY=coordinates[4], maxZ=coordinates[5];
                    }
                    assert_(minX<maxX && minY<maxY && minZ<maxZ && maxX<=volume.x && maxY<=volume.y && maxZ<=volume.z);
                    volume.x = maxX-minX, volume.y = maxY-minY, volume.z = maxZ-minZ;
                }
                volume.maximum = (1<<(8*output.sampleSize))-1;
            } else { // Inherit initial format from previous operation
                const Volume& source = inputs.first()->volume;
                volume.x=source.x, volume.y=source.y, volume.z=source.z, volume.copyMetadata(source);
            }
            volume.sampleSize = output.sampleSize;
            assert(volume.size() * volume.sampleSize);
            if(volumes.contains(output.name)) {
                rename(name+"."_+output.name+"."_+volumeFormat(volumes[volumes.indexOf(output.name)]->volume), name+"."_+output.name, memoryFolder);
                volumes.removeAll(output.name);
            } else if(trash.contains(output.name)) {
                rename(name+"."_+output.name+"."_+volumeFormat(trash[trash.indexOf(output.name)]->volume), name+"."_+output.name, memoryFolder);
                trash.removeAll(output.name);
            } else {
                assert(!existsFile(name+"."_+output.name+"."_+volumeFormat(volume), memoryFolder), output.name, volumes);
                for(uint i: range(trash.size)) { // Tries to recycle pages (avoid zeroing)
                    const VolumeData& data = trash[i];
                    string path = name+"."_+data.name+"."_+volumeFormat(data.volume);
                    assert_(existsFile(path, memoryFolder), path);
                    rename(path, name+"."_+output.name, memoryFolder);
                    trash.removeAt(i);
                    break;
                }
            }
            // Creates (or resizes) and maps a volume file for the current operation data
            File file(name+"."_+output.name, memoryFolder, Flags(ReadWrite|Create));
            file.resize( volume.size() * volume.sampleSize );
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            volume.data = buffer<byte>(data.map);
            assert(volume && volume.data.size == volume.size()*volume.sampleSize);
            volumes << move(data);
            outputs << &volumes.last();
        }
        assert_(outputs);
        Volume& target = outputs.first()->volume;
        uint X = target.x, Y = target.y, Z = target.z, XY=X*Y;

        Time time;
        if(operation==Source) {
            if(name == "balls"_) {
                array<Ball> balls = randomBalls(target, 2*filterSize+1);
                Sample analytic;
                for(Ball ball : balls) { if(ball.radius>=analytic.size) analytic.grow(ball.radius+1); analytic[ball.radius] += 4./3*PI*ball.radius*ball.radius*ball.radius; }
                writeFile(name+".analytic.tsv"_, str(analytic), resultFolder);
            } else {
                Folder folder(source);
                Time report;
                array<string> slices = folder.list(Files);
                assert_(slices.size>=maxZ);
                uint16* const targetData = (Volume16&)target;
                for(uint z=0; z<Z; z++) {
                    if(report/1000>=2) { log(z,"/",Z, (z*XY*2/1024/1024)/(time/1000), "MB/s"); report.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
                    Tiff16(Map(slices[minZ+z],folder)).read(targetData+z*XY, minX, minY, maxX-minX, maxY-minY); // Directly decodes slice images into the volume
                }
            }
        } else {
            const Volume& source = inputs.first()->volume;
            if(operation==ShiftRight || operation==SmoothX || operation==SmoothY || operation==SmoothZ) {
                uint sampleCount = 2*filterSize+1;
                uint shift = log2(sampleCount);
                if(operation==ShiftRight) {
                    int max = ((((target.maximum*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
                    int bits = log2(nextPowerOfTwo(max));
                    int headroomShift = ::max(0,bits-16);
                    if(headroomShift) log("Shifting out",headroomShift,"least significant bits to compute sum of",sampleCount,"samples without unpacking to 32bit");
                    shiftRight(target, source, headroomShift); // Simply copies if shift = 0
                    target.maximum >>= headroomShift;
                } else if(operation==SmoothX || operation==SmoothY || operation==SmoothZ) {
                    target.maximum *= sampleCount;
                    if(operation==SmoothZ) shift=0; // not necessary
                    smooth(target, source, X,Y,Z, filterSize, shift);
                    target.maximum >>= shift;
                    int margin = target.marginY + align(4, filterSize);
                    target.marginY = target.marginZ;
                    target.marginZ = target.marginX;
                    target.marginX = margin;
                }
            }
            else if(operation==Threshold) {
                float densityThreshold=0;
                if(arguments.contains("threshold"_)) {
                    densityThreshold = toDecimal(arguments.at("threshold"_));
                    while(densityThreshold >= 1) densityThreshold /= 1<<8; // Accepts 16bit, 8bit or normalized threshold
                }
                if(!densityThreshold) {
                    if(1 || !existsFile(name+".density.tsv"_, resultFolder)) { // Computes density histogram of smoothed volume
                        Time time;
                        Sample density = histogram(source,  cylinder);
                        if(existsFolder(this->source)) density[0]=density[density.size-1]=0; // Clipping makes the minimum and maximum value most frequent
                        log("density", time);
                        writeFile(name+".density.tsv"_, toASCII(density), resultFolder);
                    }
                    Sample density = parseSample( readFile(name+".density.tsv"_, resultFolder) );

                    // Crude peak mixture estimation (Works somewhat for well separated peaks, proper way would be to use expectation maximization)
                    bool plot=true;
                    Lorentz rock = estimateLorentz(density); // Rock density is the highest peak
                    if(plot) writeFile(name+".rock.tsv"_, toASCII(sample(rock,density.size)), resultFolder);
                    Sample notrock = density - sample(rock, density.size); // Substracts first estimated peak in order to estimate second peak
                    if(plot) writeFile(name+".notrock.tsv"_, toASCII(notrock), resultFolder);
                    Lorentz pore = estimateLorentz(notrock); // Pore density is the new highest peak
                    pore.height = density[pore.position]; // Use peak height from total data (estimating on not-rock yields too low estimate because rock is estimated so wide its tail overlaps pore peak)
                    if(plot) writeFile(name+".pore.tsv"_, toASCII(sample(pore,density.size)), resultFolder);
                    Sample notpore = density - sample(pore, density.size);
                    if(plot) writeFile(name+".notpore.tsv"_, toASCII(notpore), resultFolder);
                    uint threshold=0; for(uint i=pore.position; i<rock.position; i++) if(pore[i] <= notpore[i]) { threshold = i; break; } // First intersection between pore and not-pore (same probability)
                    densityThreshold = float(threshold) / float(density.size);
                    log("Automatic threshold", densityThreshold, "between pore at", float(pore.position)/float(density.size), "and rock at", float(rock.position)/float(density.size));
                } else log("Manual threshold", densityThreshold);
                threshold(target, outputs[1]->volume, source, densityThreshold);
            }
            else if(operation==Colorize) {
                colorize(target, source, inputs[1]->volume);
            } else if(operation==DistanceX || operation==EmptyX || operation==DistanceY || operation==EmptyY) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, source, X,Y,Z);
            }
            else if(operation==DistanceZ || operation==EmptyZ) {
                perpendicularBisectorEuclideanDistanceTransform<true>(target, source, X,Y,Z);
                target.maximum=maximum((const Volume32&)target);
            }
            else if(operation==Tile) tile(target, source);
            else if(operation==Maximum) maximum(target, source);
            //else if(operation==Skeleton) integerMedialAxis(target, inputs[0]->volume, inputs[1]->volume, inputs[2]->volume);
            else if(operation==Rasterize) rasterize(target, source);
            //else if(operation==Crop) { const int size=256; crop(target, source, source.x/2-size/2, source.y/2-size/2, source.z/2-size/2, source.x/2+size/2, source.y/2+size/2, source.z/2+size/2); }
            else if(operation==ASCII) toASCII(target, source);
            else if(operation==RenderEmpty) squareRoot(target, source);
            else if(operation==RenderDensity || operation==RenderIntensity) ::render(target, source);
            else error("Unimplemented",operation);
        }
        log(operation, time);
        if(target.sampleSize==2) assert(maximum((const Volume16&)target)<=target.maximum, operation, target, maximum((const Volume16&)target), target.maximum);
        if(target.sampleSize==4) assert(maximum((const Volume32&)target)<=target.maximum, operation, target, maximum((const Volume32&)target), target.maximum);

        for(VolumeData* output: outputs) {
            Volume& target = output->volume;
            if(output->name=="distance"_) // Only packs after distance pass (FIXME: make all operations generic)
            while(target.maximum < (1ul<<(8*(target.sampleSize/2))) && target.sampleSize>2/*FIXME*/) { // Packs outputs if needed
                const Volume32& target32 = target;
                target.sampleSize /= 2;
                Time time;
                pack(target, target32);
                log("pack", time);
                output->map.unmap();
                File file(name+"."_+output->name, memoryFolder, ReadWrite);
                file.resize( target.size() * target.sampleSize);
                output->map = Map(file, Map::Prot(Map::Read|Map::Write));
                target.data = buffer<byte>(output->map);
            }
            assert(target.maximum< (1ul<<(8*target.sampleSize)));

            string outputName = name+"."_+output->name;
            rename(outputName, outputName+"."_+volumeFormat(output->volume), memoryFolder); // Renames output files (once data is valid)

            if((output->name=="maximum"_ || output->name=="walk"_) && (1 || !existsFile(outputName+".tsv"_, resultFolder))) {
                Time time;
                Sample histogram = sqrtHistogram(output->volume, cylinder);
                histogram[0] = 0; // Clears background (rock) voxel count to plot with a bigger Y scale
                float scale = toDecimal(arguments.value("resolution"_,"1"_));
                writeFile(outputName+".tsv"_, toASCII(histogram, scale), resultFolder);
                log("√histogram", output->name, time);
            }
        }
        // Recycles all inputs (avoid zeroing new pages) (FIXME: prevent recycling of inputs also being used by other pending operations)
        if(operation != *operations.last()) for(const VolumeData* input: inputs) if(!selection.contains(input->name)) trash << volumes.take(volumes.indexOf(input->name));

        return &volumes[volumes.indexOf(targetName)];
    }

    /// Shows an image corresponding to the volume slice at Z position \a index
    void setSlice(float slice) {
        slice = clip(0.f, slice, 1.f);
        if(currentSlice != slice) { currentSlice = slice; updateView(); }
    }

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) {
        if(button==WheelDown||button==WheelUp) {
            int index = clip<int>(0,selection.indexOf(current->name)+(button==WheelUp?1:-1),selection.size-1);
            current = getVolume(selection[index]);
            updateView();
            return true;
        }
        if(renderVolume) {
            if(!button) return false;
            int2 delta = cursor-lastPos;
            lastPos = cursor;
            if(event != Motion) return false;
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y); //TODO: warp
            rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        } else {
            setSlice(float(cursor.x)/(size.x-1));
        }
        updateView();
        return true;
    }

    void updateView() {
        assert(current);
        int2 size(current->volume.x,current->volume.y);
        while(2*size<displaySize) size *= 2;
        if(window.size != size) window.setSize(size);
        else window.render();
    }

    void render(int2 position, int2 size) {
        assert(current);
        const Volume& source = current->volume;
        if(source.sampleSize==20) { exit(); return; } // Don't try to display ASCII
        uint z = source.marginZ+(source.z-2*source.marginZ-1)*currentSlice;
        assert(z >= source.marginZ && z<source.z-source.marginZ);
        if(!renderVolume) {
            Image image = slice(source, z, cylinder);
            while(2*image.size()<=size) image=upsample(image);
            blit(position, image);
        } else {
            mat3 view;
            view.rotateX(rotation.y); // pitch
            view.rotateZ(rotation.x); // yaw
            const Volume& empty = getVolume("empty"_)->volume;
            const Volume& density = getVolume("density"_)->volume;
            const Volume& intensity = getVolume("intensity"_)->volume;
            Time time;
            assert_(position==int2(0) && size == framebuffer.size());
            ::render(framebuffer, empty, density, intensity, view);
#if 0
            log((uint64)time,"ms");
            window.render(); // Force continuous updates (even when nothing changed)
            wait.reset();
#endif
        }
    }

    // Arguments
    ref<byte> source; // Path to folder containing source slice images (or name of a validation case (balls, cylinders, cones))
    uint minX=0, minY=0, minZ=0, maxX=0, maxY=0, maxZ=0; // Coordinates to crop source volume
    bool cylinder = false; // Whether to clip histograms computation and slice rendering to the inscribed cylinder
    Folder memoryFolder = "dev/shm"_; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate operations of volume data (up to 32bit per sample)
    ref<byte> name; // Used to name intermediate and output files (folder base name)
    uint filterSize = 2; // Smooth operation averages samples in a (2×filterSize+1)³ window
    Folder resultFolder = "ptmp"_; // Folder where smoothed density and pore size histograms are written
    ref<byte> result; // Path to file (or folder) where target volume data is copied
    array<ref<byte>> selection; // Data selection for reviewing (mouse wheel selection) (also prevent recycling)
    map<ref<byte>,ref<byte>> arguments;

    // Variables
    array<unique<VolumeData>> volumes; // Mapped volume with valid data
    array<unique<VolumeData>> trash; // Mapped volume with valid data, not being needed anymore, ready to be recycled
    const VolumeData* current=0;
    float currentSlice=0; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

    bool renderVolume = false;
    int2 lastPos;
    vec2 rotation = vec2(PI/3,-PI/3); // Current view angles (yaw,pitch)
} app( arguments() );
