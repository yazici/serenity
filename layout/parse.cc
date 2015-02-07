#include "parse.h"
#include "file.h"
#include "text.h"
#include "ui/render.h"
#include "thread.h"

struct ImageElement : Element {
	Map file;
	ImageElement(string fileName, const Folder& folder) : file(fileName , folder) {
		vec2 size = vec2(::imageSize(file));
		aspectRatio = (float)size.x/size.y;
	}
	Image image(float) const override {
		Image image = decodeImage(file);
		image.alpha = false;
		return image;
	}
};
struct TextElement : Element {
	String string;
	bool center = true;
	float textSize = 12;
	bool transpose = false;
	TextElement(::string text) : string(copyRef(text)) {
		vec2 size = -Text(text, textSize/72*72, white, 1, 0, "LinLibertine", false, 1, center).sizeHint();
		if(transpose) swap(size.x, size.y);
		aspectRatio = (float)size.x/size.y;
	}
	Image image(float mmPx) const override {
		int2 size = this->size(mmPx);
		if(transpose) swap(size.x, size.y);
		Text text(string, textSize/72*inchMM*mmPx, white, 1, size.x, "LinLibertine", false, 1, center);
		Image image = render(size, text.graphics(vec2(size)));
		if(transpose) image = rotate(image);
		return image;
	}
};

#undef assert
#define assert(expr, message...) ({ if(!(expr)) { error(#expr ""_, ## message); return; } })

LayoutParse::LayoutParse(const Folder& folder, TextData&& s, function<void(string)> logChanged, FileWatcher* watcher)
	: logChanged(logChanged) {
	const ref<string> parameters = {"page-size","outer","inner","same-outer","same-inner","same-size","chroma","intensity","hue"};
	// -- Parses arguments
	for(;;) {
		int nextLine = 0;
		while(s && s[nextLine] != '\n') nextLine++;
		string line = s.peek(nextLine);
		if(startsWith(line, "#")) { s.until('\n'); continue; }
		if(line.contains('=')) {
			string key = s.whileNo(" \t\n=");
			assert(parameters.contains(key), "Unknown parameter", key, ", expected", parameters);
			s.whileAny(" \t");
			s.skip("=");
			s.whileAny(" \t");
			string value = s.whileNo(" \t\n");
			s.whileAny(" \t");
			s.skip("\n");
			arguments.insert(copyRef(key), copyRef(value));
		} else break;
	}
	size = argument<vec2>("size"), margin = value<vec2>("margin"_, 20), space = value<vec2>("space"_, 15);
	s.whileAny(" \n");

	// -- Parses table
	array<String> files = folder.list(Files);
	files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
	array<array<int>> rows;

	while(s) {
		array<int> row;
		while(!s.match("\n")) {
			if(s.match("#")) { s.until('\n'); continue; }
			/***/ if(s.match("-")) row.append(-1);
			else if(s.match("|")) { columnStructure=true; row.append(-2); }
			else if(s.match("\\")) row.append(-3);
			else {
				row.append(elements.size); // Appends element index to row
				unique<Element> element = nullptr;
				/**/ if(s.match("@")) { // Indirect text
					string name = s.whileNo(" \t\n");
					unique<TextElement> text(readFile(name));
					text->center = false;
					element = move(text);
					if(watcher) watcher->addWatch(name);
				} else if(s.match("\"")) { // Text
					unique<TextElement> text (replace(s.until('"'),"\\n","\n"));
					string textSize = s.whileDecimal();
					text->textSize = textSize ? parseDecimal(textSize) : 12;
					text->transpose = s.match("T");
					element = move(text);
				} else { // Image
					string name = s.whileNo("! \t\n");
					string file = [&](string name) { for(string file: files) if(startsWith(file, name)) return file; return ""_; }(name);
					if(!file) { error("No such image"_, name, "in", files); return; }
					if(watcher) watcher->addWatch(file);
					element = unique<ImageElement>(file, folder);
				}
				element->index = int2(row.size-1, rows.size);
				if(s.match("!")) element->anchor.x = 1./2;
				if(element->aspectRatio<0) freeAspects.append(elements.size);
				if(element->anchor.x) horizontalAnchors.append(elements.size);
				if(element->anchor.y) verticalAnchors.append(elements.size);
				elements.append(move(element));
			}
			s.whileAny(" \t"_);
		}
		assert(row);
		// Automatically generate table structure from row-wise definition
		for(auto& o : rows) {
			if(o.size < row.size) { assert(o.size==1); o.append(-1); rowStructure=true; gridStructure=false; }
			if(row.size < o.size) { row.append(-1); rowStructure=true; gridStructure=false; }
		}
		rows.append(move(row));
		if(!s) break;
	}
	if(rows.size==1) rowStructure=true;
	assert(rows);
	size_t columnCount = rows[0].size;
	table = int2(columnCount, rows.size);
	for(size_t y: range(rows.size)) {
		assert(rows[y].size == columnCount);
		for(size_t x: range(columnCount)) {
			if(rows[y][x] >= 0) {
				table(x, y) = {size_t(rows[y][x]), int2(x, y), 1, false, false};
			} else {
				Cell& cell = table(x, y);
				if(rows[y][x] == -1) cell.horizontalExtension = true;
				if(rows[y][x] == -2) cell.verticalExtension = true;
				if(rows[y][x] == -3) cell.horizontalExtension = true, cell.verticalExtension = true;
				int2 size = 1;
				while(rows[y][x] == -3) x--, y--, size.x++, size.y++;
				while(rows[y][x] == -2) y--, size.y++;
				while(rows[y][x] == -1) x--, size.x++;
				Cell& parent = table(x, y);
				parent.parentSize = max(parent.parentSize, size);
				cell.parentIndex = parent.parentIndex;
			}
		}
	}
	for(Cell& cell : table.cells) {
		cell.parentElementIndex = table(cell.parentIndex).parentElementIndex;
		cell.parentSize = table(cell.parentIndex).parentSize;
		assert_(elements[cell.parentElementIndex]->index == cell.parentIndex);
		elements[cell.parentElementIndex]->cellCount = cell.parentSize;
	}
	if(table.columnCount == 1) columnStructure=true;
	if(table.columnCount == rows.size) gridStructure = true;
	log(size, margin, space, strx(table.size), gridStructure?"grid":"", rowStructure?"row":"", columnStructure?"column":"");
}
