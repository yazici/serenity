#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "zip.h"
#include "tiff.h"
#include "flic.h"
#include "time.h"
FILE(shader)

/// 2D array of floating-point pixels
struct ImageF : buffer<float> {
	int2 size = 0;
	ImageF(int2 size) : buffer((size_t)size.y*size.x), size(size) { assert(size>int2(0)); }
	inline float& operator()(size_t x, size_t y) const { assert(x<uint(size.x) && y<uint(size.y), x, y); return at(y*size.x+x); }
};

struct TriangleStrip : buffer<uint> {
	uint restart = 0xFFFFFFFF;
	uint last[2] = {restart, restart};
	bool even = true;

	TriangleStrip(uint capacity) : buffer<uint>(capacity, 0) {}
	void operator()(uint v0, uint v1, uint v2) {
		assert_(v0 < restart && v1 < restart && v2 < restart);
		if(last[0] == v0 && last[1] == v1) append(v2);
		else {
			if(size) append(restart);
			append(v0); append(v1); append(v2);
			even = true;
		}
		if(even) last[0] = v2, last[1] = v1; else last[0] = v0, last[1] = v2;
		even = !even;
	}
};

struct Terrain : Poll {
	GLShader shader {::shader(), {"terrain"}};

	struct Tile {
		int level;
		int2 index;
		int2 size = 0; /*without +1 repeated row/column*/
		struct Range { int min=0, max=0; } zRange;
		buffer<float> firstRowColumn; // To fill last row/column of previous tile
		GLBuffer elevation;
		bool loaded = false;
		GLVertexArray vertexArray;
		GLTexture textureBuffer; // shader read access (texelFetch) to elevation buffer
		GLIndexBuffer indexBuffer;
		Tile(int level, int2 index) : level(level), index(index) {}
		void decode() { // Decodes elevation data and uploads to GPU
			string name = 0 ? "dem15"_ : "globe30"_;
			Map map(str(index.x, 2u,'0')+","+str(index.y,2u,'0'), Folder(name+"."+str(1<<level)+".eg2rle6"));
			FLIC source(map);
			ImageF elevation (source.size+int2(1));
			firstRowColumn = buffer<float>(source.size.x+source.size.y);
			for(int y: range(source.size.y)) {
				int16 line[source.size.x];
				source.read(mref<int16>(line, source.size.x));
				for(int x: range(source.size.x)) {
					int z = line[x];
					elevation(x, y) = z;
					if(y==0) firstRowColumn[x] = elevation(x, 0);
					zRange.min = ::min(zRange.min, z);
					zRange.max = ::max(zRange.max, z);
				}
				firstRowColumn[source.size.x+y] = elevation(0, y);
			}
			size = source.size;
			this->elevation = GLBuffer(elevation);
			//log("Decode", level, index);
		}
	};
	array<unique<Tile>> tiles;
	Lock lock;

	Lock queueLock;
	array<Tile*> loadQueue; // LIFO

	function<void()> contentChanged;

	/// Returns tile (instances if missing)
	Tile& tile(int level, int2 index) {
		assert_(level <= 5);
		index.x = index.x%(2*(1<<level)); // Wraps longitude
		assert_(0 <= index.x && index.x < 2*(1<<level));
		assert_(0 <= index.y && index.y < 1*(1<<level), index.y);
		Locker tilesLock(this->lock); // Keeps decoder from reallocating 'tiles' while within for(tiles) loop
		for(Tile& tile: tiles) if(tile.level == level && tile.index==index) return tile;
		return tiles.append(unique<Tile>(level, index));
	}

	/// Returns first row/column data to stitch tiles together (decodes if missing)
	ref<float> firstRowColumn(int level, int2 index) {
		Tile& tile = this->tile(level, int2(index.x, index.y));
		if(!tile.firstRowColumn) tile.decode();
		return tile.firstRowColumn;
	}

	/// Completes tile for display by stitching with neighbours and uploading an index buffer
	// TODO: correct multiresolution stitch
	// TODO: share a single index buffer between all full tiles
	void load(Tile& tile) {
		if(tile.loaded) return;
		if(!tile.firstRowColumn) tile.decode();
		log("Load", tile.level, tile.index);

		// Completes last row/column with first row/column of next neighbours
		auto elevation = tile.elevation.map<float>();

		if(tile.index.y+1 < (1<<tile.level)) {
			ref<float> nextY = firstRowColumn(tile.level, int2(tile.index.x, tile.index.y+1));
			for(size_t x: range(tile.size.x)) elevation[(size_t)tile.size.y*(tile.size.x+1)+x] = nextY[x];
		} else {
			for(size_t x: range(tile.size.x)) elevation[(size_t)tile.size.y*(tile.size.x+1)+x] = 0; // Pole
		}

		ref<float> nextX = firstRowColumn(tile.level, int2(tile.index.x+1, tile.index.y+0));
		for(size_t y: range(tile.size.y)) elevation[y*(tile.size.x+1)+tile.size.x] = nextX[tile.size.x+y];

		if(tile.index.y+1 < (1<<tile.level)) {
			ref<float> nextXY = firstRowColumn(tile.level, int2(tile.index.x+1, tile.index.y+1));
			elevation[tile.size.y*(tile.size.x+1)+tile.size.x] = nextXY[0];
		} else {
			elevation[tile.size.y*(tile.size.x+1)+tile.size.x] = 0; // Pole
		}

		struct TriangleStrip triangleStrip (tile.size.x*tile.size.y*6);
		for(int y: range(tile.size.y)) for(int x: range(tile.size.x)) { // TODO: Z-order triangle strips for framebuffer locality ?
			int v00 = (y+0)*(tile.size.x+1)+(x+0), v10 = (y+0)*(tile.size.x+1)+(x+1);
			int v01 = (y+1)*(tile.size.x+1)+(x+0), v11 = (y+1)*(tile.size.x+1)+(x+1);
			const int invalid = tile.zRange.min;
			if(elevation[v00]!=invalid && elevation[v11]!=invalid) {
				if(elevation[v00]!=invalid) triangleStrip(v10, v00, v11);
				if(elevation[v01]!=invalid) triangleStrip(v11, v00, v01);
			}
		}
		if(triangleStrip) { // Not a void tile
			tile.indexBuffer = triangleStrip;
			tile.textureBuffer = GLTexture(tile.elevation, tile.size+int2(1));
			tile.vertexArray = GLVertexArray();
			tile.vertexArray.bindAttribute(shader.attribLocation("aElevation"_), 1, Float, tile.elevation);
		}
		tile.loaded = true;
	}

	Terrain(Thread& thread, function<void()> contentChanged) : Poll(0,0,thread), contentChanged(contentChanged) {
		for(uint Y: range(1)) for(uint X: range(2)) load(tile(0, int2(X, Y))); // Loads mipmap level 0 (2x1 tiles)
		registerPoll(); // Keeps thread from bailing out
	}
	void event() override {
		if(!loadQueue) return;
		load(*({Locker lock(queueLock); loadQueue.pop(); }));
		if(loadQueue) Poll::queue();
		contentChanged();
	}

	bool isVisible(const mat4 viewProjection, int level, int2 index) {
		log(1<<level);
		float sphericalSize = float(PI/(1<<level));
		vec2 sphericalOrigin = float(PI/(1<<level))*vec2(index);

		// Evaluates closest tile point
		vec3 O = normalize(viewProjection.inverse()[3].xyz()); // Projects view origin (last column: VP⁻¹·(0,0,0,1)) on the sphere
		vec2 s = vec2(atan(O.y, O.x), acos(O.z)); // Spherical coordinates
		if(s.x < 0) s.x+=2*PI; // [-π,π] -> [0, 2π]
		s = clamp(sphericalOrigin, s, sphericalOrigin+vec2(sphericalSize)); // Clamps to tile
		auto sphere = [](vec2 spherical) { return vec3(sin(spherical.y)*cos(spherical.x), sin(spherical.y)*sin(spherical.x), cos(spherical.y)); };
		vec3 S = sphere(s); // Closest tile point (cartesian 'world' system)
		vec3 V = viewProjection * S; // Perspective projects to view space
		return sq(V.xy()) < 2; // View cone cull (frustum cull is not conservative as it may intersect tile without containing closest tile point)
	}

	void render(const mat4 viewProjection, vec2 size, int level, int2 index, array<Tile*>& loadQueue) {
		Tile& tile = this->tile(level, index);
		if(!tile.indexBuffer) return; // Void tile
		if(!isVisible(viewProjection, level, index)) return; // Out of view
		assert_(tile.loaded);

		float sphericalSize = float(PI/(1<<tile.level));
		float sphericalResolution = sphericalSize/tile.size.x;
		vec2 sphericalOrigin = float(PI/(1<<tile.level))*vec2(tile.index);

		// Evaluates closest tile point
		vec3 O = normalize(viewProjection.inverse()[3].xyz()); // Projects view origin (last column: VP⁻¹·(0,0,0,1)) on the sphere
		vec2 s = vec2(atan(O.y, O.x), acos(O.z)); // Spherical coordinates
		if(s.x < 0) s.x+=2*PI; // [-π,π] -> [0, 2π]
		s = clamp(sphericalOrigin, s, sphericalOrigin+vec2(sphericalSize)); // Clamps to tile
		auto sphere = [](vec2 spherical) { return vec3(sin(spherical.y)*cos(spherical.x), sin(spherical.y)*sin(spherical.x), cos(spherical.y)); };
		vec3 S = sphere(s); // Closest tile point (cartesian 'world' system)
		vec3 V = viewProjection * S; // Perspective projects to view space
		if(sq(V.xy()) >= 2) return; // View cone cull (frustum cull is not conservative as it may intersect tile without containing closest tile point)

		// Projects cell size around closest tile point to increase resolution as needed
		vec2 vx = (viewProjection * sphere(s+vec2(sphericalResolution/2, 0)) - viewProjection * sphere(s-vec2(sphericalResolution/2, 0))).xy();
		float dx = length(size*vx);
		vec2 vy = (viewProjection * sphere(s+vec2(0, sphericalResolution/2)) - viewProjection * sphere(s-vec2(0, sphericalResolution/2))).xy();
		float dy = length(size*vy);
		float d = max(dx, dy);
		if(d > 4 && tile.level<5) { // Requires finer resolution
			bool allVisibleChildrenAvailable = true;
			for(int Y: range(2)) for(int X: range(2)) { // Queues visible children tiles for loading
				if(!isVisible(viewProjection, level+1, tile.index*2+int2(X,Y))) continue;
				Tile& child = this->tile(tile.level+1, tile.index*2+int2(X,Y));
				if(child.loaded) continue;
				allVisibleChildrenAvailable = false;
				loadQueue.append(&child); // TODO: sort by d
			}
			if(allVisibleChildrenAvailable) {
				for(int Y: range(2)) for(int X: range(2)) render(viewProjection, size, tile.level+1, tile.index*2+int2(X,Y), loadQueue);
				return; // Skips parent tile
			}
		}
		shader["W"_] = tile.size.x+1;
		shader["sphericalResolution"_] = sphericalResolution;
		shader["sphericalOrigin"_] = sphericalOrigin;
		static constexpr float R = 4E7/(2*PI); // 4·10⁷/2π  ~ 6.37
		shader["R"_] = R;
		shader["tElevation"_] = 0;
		tile.textureBuffer.bind(0);
		shader["modelViewProjectionTransform"_] = mat4(viewProjection);
		tile.vertexArray.bind();
		tile.indexBuffer.draw();
	}

	void render(const mat4 viewProjection, vec2 size) {
		shader.bind();
		array<Tile*> loadQueue;
		for(uint Y: range(1)) for(uint X: range(2)) render(viewProjection, size, 0, int2(X, Y), loadQueue);
		{Locker lock (queueLock); this->loadQueue=move(loadQueue); if(this->loadQueue) Poll::queue();}
	}
};

/// Views a scene
struct View : Widget {
	Window window {this, int2(0, 1024), []{ return "Editor"__; }};
	Thread thread;
	Job initializeThreadGLContext {thread, [this]{ window.initializeThreadGLContext(); }};
	Terrain terrain {thread, {&window, &Window::render}};

	// View
	vec2 lastPos; // Last cursor position to compute relative mouse movements
	vec2 rotation = vec2(0, 0); // Current view angles (longitude, latitude)
	float altitude = 1;

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
		 vec2 delta = cursor-lastPos; lastPos=cursor;
		 if(event==Motion && button==LeftButton) {
			 rotation += delta / size / float(asin(1/(1+altitude)));
			 rotation.y = clamp<float>(-PI, rotation.y, 0);
		 }
		 else if(event==Press && button==WheelUp) altitude = max(1./256, altitude / pow(2, 1./8));
		 else if(event==Press && button==WheelDown) altitude = min(1., altitude * pow(2, 1./8));
		 else return false;
		 return true;
	}

	vec2 sizeHint(vec2) override { return 0; }
	View() {
		glDepthTest(true);
		glCullFace(true);
		thread.spawn(); // Spawns tile loading (decode and upload) thread
	}
	shared<Graphics> graphics(vec2 unused size) override {
		mat4 projection = mat4().perspective(PI/3, size, altitude-1./512 /*maximum elevation*/, altitude+1);
		mat4 view = mat4()
				.translate(vec3(0,0,-altitude-1)) // Altitude
				.rotateX(rotation.y) // Latitude
				.rotateZ(rotation.x) // Longitude
				;
		mat4 viewProjection = projection*view;
		terrain.render(viewProjection, size);
		window.setTitle(str(terrain.loadQueue.size));
		return nullptr;
	}
} app;
