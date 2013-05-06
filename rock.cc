#include "process.h"
#include "time.h"
#include "tiff.h"
#include "smooth.h"
#include "threshold.h"
#include "histogram.h"
#include "distance.h"
#include "maximum.h"
#include "window.h"
#include "interface.h"
//#include "render.h"

static array<struct Pass*> passes;
struct Pass {
    Pass(const ref<byte>& name, uint sampleSize) : name(name) { outputs<<Output{name,sampleSize}; passes << this; }
    Pass(const ref<byte>& input, const ref<byte>& name, uint sampleSize) : Pass(name, sampleSize) { inputs<<input; }
    Pass(const ref<byte>& input, const ref<byte>& name, uint sampleSize, const ref<byte>& output2, uint sampleSize2) : Pass(input, name, sampleSize) {
        outputs<<Output{output2,sampleSize2}; }

    ref<byte> name;
    array<ref<byte>> inputs;
    struct Output { ref<byte> name; uint sampleSize; }; array<Output> outputs;
};
const ref<byte>& str(const Pass& pass) { return pass.name; }
bool operator==(const Pass& a, const Pass& b) { return a.name == b.name; }
const Pass* passForOutput(const ref<byte>& name) { for(Pass* pass: passes) for(const Pass::Output& output: pass->outputs) if(output.name==name) return pass; return 0; }

Pass Source("source"_,2); // Loads from original image slices
Pass ShiftRight ("source"_,"shift"_,2); // Shifts data to avoid overflows
Pass SmoothX ("shift"_,"smoothx"_,2); // Denoises data and filters small pores by averaging samples in a window (X pass)
Pass SmoothY("smoothx"_,"smoothy"_,2);  // Y pass
Pass SmoothZ("smoothy"_,"smoothz"_,2); // Z pass
Pass Threshold("smoothz"_,"threshold"_,4); // Segments in rock vs pore space by comparing to a fixed threshold
Pass DistanceX("threshold"_,"distancex"_,4,"positionx"_,2); // Computes field of distance to nearest rock wall (X pass)
Pass DistanceY("distancex"_,"distancey"_,4,"positiony"_,2); // Y pass
Pass DistanceZ("distancey"_,"distancez"_,4,"positionz"_,2); // Z pass
Pass Tile("distancez"_,"tile"_,2); // Layouts volume in Z-order to improve locality on maximum search
Pass Maximum("tile"_,"maximum"_,2); // Computes field of nearest local maximum of distance field (i.e field of maximum enclosing sphere radii)

struct VolumeData {
    VolumeData(const ref<byte>& name):name(name){}
    string name;
    Map map;
    Volume volume;
};
bool operator ==(const VolumeData& a, const ref<byte>& name) { return a.name == name; }

/// From an X-ray tomography volume, segments rocks pore space and computes histogram of pore sizes
struct Rock : Widget {
    Rock(const ref<byte>& path, const ref<byte>& target, const ref<ref<byte>>& force) : folder(path), name(section(path,'/',-2,-1)) {
        assert(name);
        for(const string& path: memoryFolder.list(Files)) { // Maps intermediate data from any previous run
            if(!startsWith(path, name)) continue;
            VolumeData data = section(path,'.',-3,-2);
            if(!passForOutput(data.name) || volumes.contains(data.name) || force.contains(passForOutput(data.name)->name)) { remove(path, memoryFolder); continue; } // Removes invalid, multiple or forced data
            parseVolumeFormat(data.volume, path);
            File file = File(path, memoryFolder, ReadWrite);
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            data.volume.data = buffer<byte>(data.map);
            assert( data.volume );
            volumes << move(data);
        }
        current = &getVolume(target)->volume;
        updateView();

        window.localShortcut(Escape).connect(&exit);
        window.clearBackground = false;
        window.show();
    }

    /// Computes target volume
    const VolumeData* getVolume(const ref<byte>& targetName) {
        for(const VolumeData& data: volumes) if(data.name == targetName) return &data;
        const Pass& pass = *passForOutput(targetName);
        assert(&pass);
        array<const VolumeData*> inputs;
        for(const ref<byte>& input: pass.inputs) inputs << getVolume( input );

        array<VolumeData*> outputs;
        for(const Pass::Output& output: pass.outputs) {
            VolumeData data = output.name;
            Volume& volume = data.volume;
            if(!pass.inputs) { // Original source slices format
                array<string> slices = folder.list(Files);
                Map file (slices.first(), folder);
                const Tiff16 image (file);
                volume.x = image.width, volume.y = image.height, volume.z = slices.size, volume.den = (1<<(8*output.sampleSize))-1;
            } else { // Inherit initial format from previous pass
                const Volume& source = inputs.first()->volume;
                volume.x=source.x, volume.y=source.y, volume.z=source.z, volume.copyMetadata(source);
            }
            volume.sampleSize = output.sampleSize;
            assert(volume.size() * volume.sampleSize);
            assert(!existsFile(name+"."_+output.name+"."_+volumeFormat(volume), memoryFolder), output.name); // Would have been loaded
            for(uint i: range(thrash.size)) { // Tries to recycle pages (avoid zeroing)
                const VolumeData& data = thrash[i];
                string path = name+"."_+data.name+"."_+volumeFormat(data.volume);
                assert_(existsFile(path, memoryFolder), path);
                rename(path, name+"."_+output.name, memoryFolder);
                thrash.removeAt(i);
                break;
            }
            // Creates (or resizes) and maps a volume file for the current pass data
            File file(name+"."_+output.name, memoryFolder, Flags(ReadWrite|Create));
            file.resize( volume.size() * volume.sampleSize );
            data.map = Map(file, Map::Prot(Map::Read|Map::Write));
            volume.data = buffer<byte>(data.map);
            assert(volume && volume.data.size == volume.size()*volume.sampleSize);
            volumes << move(data);
            outputs << &volumes.last();
        }
        Volume& target = outputs.first()->volume;
        Volume* secondary = outputs.size>1 ? &outputs[1]->volume : 0;
        uint X = target.x, Y = target.y, Z = target.z, XY=X*Y;

        Time time;
        if(pass==Source) {
            array<string> slices = folder.list(Files);
            Time time;
            uint16* const targetData = (Volume16&)target;
            for(uint z=0; z<slices.size; z++) {
                if(time/1000>=2) { log(z,"/",slices.size); time.reset(); } // Reports progress every 2 second (initial read from a cold drive may take minutes)
                Tiff16(Map(slices[z],folder)).read(targetData+z*XY); // Directly decodes slice images into the volume
            }
        } else {
            const Volume& source = inputs.first()->volume;

            if(pass==ShiftRight || pass==SmoothX || pass==SmoothY || pass==SmoothZ) {
                constexpr int sampleCount = 2*filterSize+1;
                constexpr uint shift = log2(sampleCount);
                if(pass==ShiftRight) {
                    int max = (((((target.den/target.num)*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
                    int bits = log2(nextPowerOfTwo(max));
                    int shift = ::max(0,bits-16);
                    if(shift) log("Shifting out",shift,"least significant bits to compute sum of",sampleCount,"samples without unpacking to 32bit");
                    shiftRight(target, source, shift); // Simply copies if shift = 0
                    target.den >>= shift;
                }
                else if(pass==SmoothX) {
                    smooth(target, source, X,Y,Z, filterSize, shift);
                    target.den *= sampleCount; target.den >>= shift;
                    target.marginX += align(4, filterSize);
                }
                else if(pass==SmoothY) {
                    smooth(target, source, Y,Z,X, filterSize, shift);
                    target.den *= sampleCount; target.den >>= shift;
                    target.marginY += align(4, filterSize);
                }
                else if(pass==SmoothZ) {
                    smooth(target, source, Z,X,Y, filterSize, 0);
                    target.den *= sampleCount;
                    target.marginZ += align(4, filterSize);
                }
            }
            else if(pass==Threshold) {
                if(1 || !existsFile(name+".density.tsv"_, resultFolder)) { // Computes density histogram of smoothed volume
                    Time time;
                    writeFile(name+".density.tsv"_, str(histogram(source)), resultFolder);
                    log("density", time);
                }
                Histogram density = parseHistogram( readFile(name+".density.tsv"_, resultFolder) );
                // Use the minimum between the two highest maximum of density histogram as density threshold
                struct { uint density=0, count=0; } max[2];
                for(uint i=1; i<density.binCount-1; i++) {
                    if(density[i-1] < density[i] && density[i] > density[i+1] && density[i] > max[0].count) {
                        max[0].density = i, max[0].count = density[i];
                        if(max[0].count > max[1].count) swap(max[0],max[1]);
                    }
                }
                uint densityThreshold; uint minimum = -1;
                for(uint i=max[0].density; i<max[1].density; i++) {
                    if(density[i] < minimum) densityThreshold = i, minimum = density[i];
                }
                log("Using threshold",densityThreshold,"between pore at",max[0].density,"and rock at",max[1].density);
                threshold(target, source, float(densityThreshold) / float(density.binCount));
            }
            else if(pass==DistanceX) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, *secondary, source, X,Y,Z);
            }
            else if(pass==DistanceY) {
                perpendicularBisectorEuclideanDistanceTransform<false>(target, *secondary, source,  Y,Z,X);
            }
            else if(pass==DistanceZ) {
                perpendicularBisectorEuclideanDistanceTransform<true>(target, *secondary, source,  Z,X,Y);
                target.num = 1, target.den=maximum((const Volume32&)target);
            }
            else if(pass==Tile) {
                tile(target, source);
            }
            else if(pass==Maximum) {
                maximum(target, source);
            }
            else error("Unimplemented",pass);
        }
        log(pass, time);

        for(VolumeData* output: outputs) {
            Volume& target = output->volume;
            while(target.den/target.num < (1ul<<(8*(target.sampleSize/2)))) { // Packs outputs if needed
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
            assert(target.den/target.num < (1ul<<(8*target.sampleSize)), target.num, target.den, target.den/target.num, 1ul<<(8*target.sampleSize));

            rename(name+"."_+output->name, name+"."_+output->name+"."_+volumeFormat(output->volume), memoryFolder); // Renames output files (once data is valid)
        }
        // Recycles all inputs (avoid zeroing new pages)
        for(const VolumeData* input: inputs) thrash << volumes.take(volumes.indexOf(input->name));

        if(pass==Source) writeFile(name+".raw_density.tsv"_, str(histogram(target)), resultFolder);
        if(pass==Maximum && (1 || !existsFile(name+".radius.tsv"_, resultFolder))) {
            Histogram histogram = sqrtHistogram(target);
            histogram[0] = 0; // Clears rock space voxel count to plot with a bigger Y scale
            writeFile(name+".radius.tsv"_, str(histogram), resultFolder);
            log("Pore size histogram written to",name+".radius.tsv"_);
        }
        return &volumes[volumes.indexOf(targetName)];
    }

    /// Shows an image corresponding to the volume slice at Z position \a index
    void setSlice(float slice) {
        slice = clip(0.f, slice, 1.f);
        if(currentSlice != slice) { currentSlice = slice; updateView(); }
    }

    bool mouseEvent(int2 cursor, int2 size, Event, Button) {
        setSlice(float(cursor.x)/(size.x-1));
        return true;
    }

    void updateView() {
        assert(current);
        int2 size (current->x-2*current->marginX,current->y-2*current->marginY);
        while(2*size<displaySize) size *= 2;
        window.setSize(size);
        window.render();
    }

    void render(int2 position, int2) {
        assert(current);
        uint z = current->marginZ+(current->z-2*current->marginZ-1)*currentSlice;
        Image image = current->squared ? slice(*current, z) : squareRoot(*current, z);
        blit(position, image); //FIXME: direct slice->shm
    }

    // Settings
    static constexpr uint filterSize = 8; // Smooth pass averages samples in a (2×filterSize+1)³ window
    const Folder memoryFolder {"dev/shm"_}; // Should be a RAM (or local disk) filesystem large enough to hold up to 2 intermediate passes of volume data (up to 32bit per sample)
    const Folder resultFolder {"ptmp"_}; // Final results (histograms) are written there

    // Arguments
    Folder folder; // Contains source slice images
    string name; // Used to name intermediate and output files (folder base name)

    // Variables
    array<unique<VolumeData>> volumes;
    array<unique<VolumeData>> thrash;
    const Volume* current=0;
    float currentSlice=0; // Normalized z coordinate of the currently shown slice
    Window window {this,int2(-1,-1),"Rock"_};

} app( arguments()[0], arguments().size>1 ? arguments()[1] : passes.last()->name, arguments().size>1 ? arguments().slice(1) : array<ref<byte>>() );
