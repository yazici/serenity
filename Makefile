CC = g++-4.8.0-alpha20120304
PREFIX = /usr

ifeq (,$(TARGET))
 TARGET = taskbar
endif

ifeq (,$(BUILD))
 BUILD = release
endif

#TODO: use dependency files (.P) to link object files .o
SRCS = core array string process

	 ifeq ($(TARGET),player)
 SRCS += vector signal stream file image png window raster font interface alsa ffmpeg resample player
 ICONS = play pause next
 INSTALL =icons/player.png  player.desktop
else ifeq ($(TARGET),browser)
 SRCS += stream file time http xml vector image png jpeg window raster font interface html browser
 ICONS = rss
else ifeq ($(TARGET),music)
 SRCS += vector stream time signal file alsa resample sequencer flac sampler midi window image font interface music
 ICONS = music music256
 INSTALL = icons/music.png music.desktop
else ifeq ($(TARGET),taskbar)
 SRCS += vector signal stream time file dbus image window raster font interface launcher taskbar
 ICONS = button shutdown
else ifeq ($(TARGET),editor)
 SRCS += file image gl window raster font editor
 GLSL = editor
 GPUS = shader
else ifeq ($(TARGET),symbolic)
 SRCS += symbolic algebra expression
else ifeq ($(TARGET),flac)
 SRCS += file vector flac codec disasm
else ifeq ($(TARGET),jpeg)
  SRCS += stream file time http vector image png jpeg
else ifeq ($(TARGET),bspline)
 SRCS += window bspline file image
endif

ifneq (,$(findstring time,$(SRCS)))
  LIBS += -lrt
endif

ifneq (,$(findstring alsa,$(SRCS)))
 LIBS += -lasound
endif

ifneq (,$(findstring png,$(SRCS)))
  LIBS += -lz
endif

ifneq (,$(findstring http,$(SRCS)))
 LIBS += -lssl
endif

ifneq (,$(findstring ffmpeg,$(SRCS)))
 LIBS += -lavformat -lavcodec
endif

ifneq (,$(findstring font,$(SRCS)))
  INCLUDES = -I/usr/include/freetype2
  LIBS += -lfreetype
endif

ifneq (,$(findstring window,$(SRCS)))
  LIBS += -lX11 -lXext
endif

ifneq (,$(findstring gl,$(SRCS)))
 FLAGS += -DGL
 LIBS += -lGL
 ifeq ($(BUILD),debug)
  FLAGS += -DGLU
  LIBS += -lGLU
 endif
endif

SRCS += $(ICONS:%=icons/%)
SRCS += $(GPUS:%=%.gpu)

#-fno-implicit-templates
FLAGS += -pipe -std=c++11 -fno-operator-names -Wall -Wextra -Wno-narrowing -Wno-missing-field-initializers -fno-exceptions -march=native

ifeq ($(BUILD),debug)
	FLAGS += -ggdb -DDEBUG -fno-omit-frame-pointer
	LIBS += -lbfd
else ifeq ($(BUILD),release)
	FLAGS += -ggdb -DDEBUG -fno-omit-frame-pointer -Ofast -fno-rtti
	LIBS += -lbfd
	#FLAGS += -Ofast -fno-rtti
else ifeq ($(BUILD),trace)
	FLAGS += -g -DDEBUG
	LIBS += -lbfd
	FLAGS += -finstrument-functions -finstrument-functions-exclude-file-list=intrin,vector -DTRACE
else ifeq ($(BUILD),memory)
	FLAGS += -ggdb -DDEBUG -Ofast -fno-omit-frame-pointer -frtti -DTRACE_MALLOC
	SRCS += memory
	LIBS += -lbfd
endif

all: prepare $(SRCS:%=$(BUILD)/%.o)
	$(CC) $(SRCS:%=$(BUILD)/%.o) $(LIBS) -o $(BUILD)/$(TARGET)

$(BUILD)/%.o : %.cc
	$(CC) $(FLAGS) $(INCLUDES) -c -MD -o $@ $<
	@cp $(BUILD)/$*.d $(BUILD)/$*.P; \
	sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' -e 's/$$/ :/' < $(BUILD)/$*.d >> $(BUILD)/$*.P; \
	rm -f $(BUILD)/$*.d

-include $(SRCS:%=$(BUILD)/%.P)

#Build GLSL compiler frontend
$(BUILD)/glsl: string.cc file.cc glsl.cc
	$(CC) $(FLAGS) -DNO_BFD string.cc file.cc glsl.cc -lX11 -lGL -o $(BUILD)/glsl

$(BUILD)/%.gpu.o: $(GLSL).glsl $(BUILD)/glsl
	$(BUILD)/glsl $*.gpu $(GLSL).glsl $*
	ld -r -b binary -o $@ $*.gpu
	rm -f $*.gpu

$(BUILD)/%.o: %.png
	ld -r -b binary -o $@ $<

prepare:
	@mkdir -p $(BUILD)/icons
	@ln -sf $(TARGET).files serenity.files

clean:
	rm $(BUILD)/*.o

install_icons/%.png: icons/%.png
	cp $< $(PREFIX)/share/icons/hicolor/32x32/apps

install_%.desktop: %.desktop
	cp $< $(PREFIX)/share/applications/

install: all $(INSTALL:%=install_%)
	cp $(BUILD)/$(TARGET) $(PREFIX)/bin/$(TARGET)
