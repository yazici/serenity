#include "process.h"
#include "file.h"
#include "media.h"
#include "interface.h"

ICON(play);
ICON(pause);
ICON(next);

struct Player : Application {
	AudioFile file;
	AudioOutput audio;
	array<string> folders;
	array<string> files;

	VBox layout;
	Window window = Window(int2(640,995),layout);
	 HBox toolbar;
	  ToggleButton playButton = ToggleButton(playIcon,pauseIcon);
	  TriggerButton nextButton = TriggerButton(nextIcon);
	  Text elapsed = Text(16);
	  Slider slider;
	  Text remaining = Text(16);
	 HBox main;
	  TextList albums; TextList titles; TextList durations;
	uint playHotKey = window.addHotKey(_("XF86AudioPlay"));

	void start(array<string>&& arguments) {
		elapsed.setText(_("00:00")); remaining.setText(_("00:00"));
		toolbar << &playButton << &nextButton << &elapsed << &slider << &remaining;
		main << &albums << &titles;
		layout << &toolbar << &main;
		albums.margin=0; titles.mayScroll=true;

        window.hotKeyTriggered.connect(this, &Player::hotKey);
		playButton.toggled.connect(this, &Player::togglePlay);
		nextButton.triggered.connect(this, &Player::next);
		slider.valueChanged.connect(this, &Player::seek);
		file.timeChanged.connect(this, &Player::update);
		albums.currentChanged.connect(this, &Player::playAlbum);
		titles.currentChanged.connect(this, &Player::play);
		audio.setInput(&file);

		folders = listFiles(_("/root/Music"));
		for(auto&& folder : folders) albums << Text(10,section(folder,'/',-2,-1));

		for(auto&& path: arguments) {
			assert(exists(path),"Invalid URL",path);
			if(isDirectory(path)) playAlbum(path); else appendFile(move(path));
		}
		if(files.size) next(); else { layout.update(); window.render(); }
	}
    void hotKey(uint key) {
		if(key == playHotKey) togglePlay(!playButton.enabled);
	}
	void appendFile(string&& path) {
		files << move(path);
		string title = section(section(path,'/',-2,-1),'.',0,-2);
		int i=0; while((title[i]<'A'||title[i]>'Z')&&(title[i]<'a'||title[i]>'z')) i++;
		titles << Text(16,title.slice(i).replace('_',' '));
	}
	void playAlbum(const string& path) {
		assert(isDirectory(path));
		array<string> files = listFiles(path,Recursive|Sort);
		for(auto&& file: files) appendFile(move(file));
		layout.update();
		window.render();
	}
	void playAlbum(int index) {
		stop();
		files.clear(); titles.items.clear();
		playAlbum(folders[index]);
	}
	void play(int index) {
		playButton.enabled=true;
		file.open(files[index]);
        window.rename(titles.active().text);
		audio.start();
	}
	void next() {
		if(titles.index+1<titles.count()) play(++titles.index);
	}
	void togglePlay(bool play) {
		if(play) { audio.start(); playButton.enabled=true; }
		else { audio.stop(); playButton.enabled=false; }
		window.render();
	}
	void stop() {
		audio.stop();
		file.close();
		elapsed.setText(_("--:--"));
		slider.value = -1;
		remaining.setText(_("--:--"));
		titles.index=-1;
	}
	void seek(int position) {
		file.seek(position);
	}
	void update(int position, int duration) {
		if(position == duration) next();
		if(!window.visible || slider.value == position) return;
		slider.value = position; slider.maximum=duration;
		elapsed.setText(toString(position/60,10,2)+_(":")+toString(position%60,10,2));
		remaining.setText(toString((duration-position)/60,10,2)+_(":")+toString((duration-position)%60,10,2));
		window.render();
	}
} player;
