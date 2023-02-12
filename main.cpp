#include <iostream>
#include <string>
#include <fstream>
#include "objsdl/objsdl.h"
#include "AudioFile/AudioFile.h"
#include "AudioFile/AudioFile.cpp"

using namespace std;

constexpr double Interval=pow(2.0, 1.0/12.0);

template<typename T>
struct SoundData
{
	vector<T> buffer;
	size_t pos;
	static void Fill(void* userdata, uint8* stream, int length)
	{
		auto& sound=*(SoundData<T>*)userdata;
		int len=min(length, int(sizeof(T)*sound.buffer.size()-sound.pos));
		memcpy(stream, (uint8*)sound.buffer.data()+sound.pos, len);
		if(len<length)
		{
			memcpy(stream+len, sound.buffer.data(), length-len);
		}
		sound.pos=(sound.pos+length)%sound.buffer.size();
	}
};

int SinWaveSound(int freq, int output_freq, int i, int volume)
{
	return sin(i*M_PI*freq/output_freq)*volume;
}

int NiceSound(int freq, int output_freq, int i, int volume)
{
	return SinWaveSound(freq, output_freq, i, volume/2)+SinWaveSound(freq*2, output_freq, i+output_freq/2, volume/4)+SinWaveSound(freq*3, output_freq, i, volume/8)+SinWaveSound(freq*5, output_freq, i+output_freq/2, volume/16)+SinWaveSound(freq*8, output_freq, i, volume/32)+SinWaveSound(freq*13, output_freq, i+output_freq/2, volume/32);
}

int Volume(int tone_lenght, int i, int volume)
{
	return volume*(tone_lenght-(i<tone_lenght*2/3?0:(i-tone_lenght*2/3)*4/3))/tone_lenght;
}

struct Tone
{
	uint32 freq, len, pos, volume;
};

class Song
{
private:
	uint32 length;
	vector<Tone> tones;
public:
	Song(uint32 length, vector<Tone> tones)
		:length(length), tones(move(tones)) {}
	void DrawOn(SDL::Renderer& rend)
	{
		for(size_t i=0; i<tones.size(); ++i)
		{
			int height=log(tones[i].freq)*200-1000;
			rend.Draw(SDL::Rect(rend.Size().x*tones[i].pos/length, rend.Size().y-height, rend.Size().x*tones[i].len/length, 2), SDL::Color(0,255,0));
		}
	}
	template<typename T>
	SoundData<T> CreateSound(uint32 output_freq, uint8 channels, T volume)
	{
		SoundData<T> sound{vector<T>(length*output_freq*2*channels/1000), 0};
		for(size_t tone=0; tone<tones.size(); ++tone)
		{
			for(size_t i=0, len=tones[tone].len*output_freq*channels/1000; i<len; ++i)
			{
				sound.buffer[i+tones[tone].pos*output_freq*channels/1000]+=NiceSound(tones[tone].freq, output_freq, i/channels, Volume(len*2, i, volume*tones[tone].volume/1000));
			}
		}
		for(size_t i=1, len=sound.buffer.size(); i<len; ++i)
		{
			if(abs(sound.buffer[i]-sound.buffer[i-1])>volume/16)
			{
				sound.buffer[i]=sound.buffer[i-1]+volume/16*(sound.buffer[i]>sound.buffer[i-1]?1:-1);
			}
		}
		return move(sound);
	}
};

Song LoadSong(istream& in)
{
	vector<Tone> tones;
	int base=0, total_lenght=0;
	string tone, length, position, volume;
	in>>base>>total_lenght;
	while(in>>tone>>length>>volume>>position)
	{
		Tone new_tone{0, stoi(length), 0, stoi(volume)};
		if(new_tone.len>0)
		{
			new_tone.freq=(tone=="rest"?0:int(pow(Interval, stoi(tone))*base));
			new_tone.pos=(position=="after"?tones.back().pos+tones.back().len:stoi(position));
			tones.push_back(new_tone);
		}
	}
	return move(Song(total_lenght, move(tones)));
}

template<typename T>
AudioFile<double>::AudioBuffer AudioFileBufferFrom(const T* sound, size_t len, uint8 channels, T divisor)
{
	AudioFile<double>::AudioBuffer result(channels);
	for(size_t i=0; i<result.size(); ++i)
	{
		result[i].resize(len/channels);
		for(size_t j=0; j<result[i].size(); ++j)
		{
			result[i][j]=double(sound[j*result.size()+i])/divisor;
		}
	}
	return move(result);
}

template<typename T>
void SaveSound(const std::string& path, const SoundData<T>& sound, uint32 freq, uint8 channels, T divisor, uint8 bit_depth)
{
	AudioFile<double> out_file;
	auto buffer=AudioFileBufferFrom(sound.buffer.data(), sound.buffer.size()/2, channels, divisor);
	out_file.setAudioBuffer(buffer);
	out_file.setSampleRate(freq);
	out_file.setBitDepth(bit_depth);
	out_file.save(path);
}

int main(int argc, char* argv[])
{
	if(argc<2)
	{
		SDL::MessageBox::Show("Chyba", "Neotevřeli jste žádný soubor! Co vám teď mám já chudák hrát?");
		return 0;
	}
	SDL::Init sdl;
	SDL::Window screen("Pokus", SDL::Rect(100,100, 1200,600), SDL::Window::Flags::Resizable);
	SDL::Renderer rend(screen);
	rend.Show();
	try
	{
		ifstream infile(argv[1]);
		Song song=LoadSong(infile);

		SoundData<int16> sound;
		SDL::Audio audio(48000, SDL::Audio::Format::S16, 1, 4096, SoundData<int16>::Fill, &sound);
		sound=song.CreateSound<int16>(audio.GetFrequency(), audio.GetChannels(), 0x7fff);
		SDL::AudioDevice dev(audio);
		dev.Play();
		SaveSound<int16>(argv[1]+".wav"s, sound, audio.GetFrequency(), audio.GetChannels(), 0x7fff, 24);
		bool repeat=true;
		while(repeat)
		{
			rend.Repaint(SDL::Color(0,0,0));
			song.DrawOn(rend);
			rend.Draw(SDL::Rect(rend.Size().x*sound.pos/sound.buffer.size(), 0, 2, rend.Size().y), SDL::Color(255,0,255));
			rend.Show();
			for(SDL::events::Event& evt: SDL::events::Handler())
			{
				if(evt.Type()==SDL::events::Type::MouseButtonDown)
				{
					sound.pos=evt.MouseButton().Position.x*sound.buffer.size()/rend.Size().x;
				}
				else if(evt.Type()==SDL::events::Type::Quit)
				{
					repeat=false;
				}
			}
			SDL::Wait(50);
		}
	}
	catch(invalid_argument& exc)
	{
		SDL::MessageBox::Show("Chyba", "Otevřený soubor má nesprávný formát!");
	}
    return 0;
}