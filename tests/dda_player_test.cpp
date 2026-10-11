// Tests for CDdaPlayer, the DDA emulation, using a fake audio engine, fake Storm file functions and fake DirectSound buffers

#include <doctest.h>

#include "DdaPlayer.h"
#include "DdaConversions.h"

#include "fakes/FakeAudioEngine.h"
#include "fakes/FakeDirectSoundBuffer.h"
#include "fakes/FakeStorm.h"

#include <thread>
#include <vector>

namespace
{
	// A player hooked up to fakes
	struct PlayerFixture
	{
		FakeAudioEngine engine;
		CDdaPlayer player;

		// Buffers live as long as the fixture, so they outlive the sounds that use them
		std::vector<std::unique_ptr<FakeDirectSoundBuffer>> buffers;

		PlayerFixture()
		{
			FakeStorm::Reset();
			player.Initialize(FakeStorm::Functions(), &engine);
		}

		~PlayerFixture()
		{
			player.StopAll();
		}

		FakeDirectSoundBuffer &NewBuffer(DWORD nSampleRate = 22050, WORD nBitsPerSample = 16, WORD nChannels = 2)
		{
			buffers.emplace_back(new FakeDirectSoundBuffer(nSampleRate, nBitsPerSample, nChannels));
			return *buffers.back();
		}

		BOOL Begin(HANDLE hFile, DWORD dwFlags = 0, DWORD dwStartPos = 0, long nVolume = SFILE_DDA_DEFAULT, long nPan = SFILE_DDA_DEFAULT, IDirectSoundBuffer *lpBuffer = NULL)
		{
			return player.Begin(hFile, dwFlags, 0x10000, dwStartPos, nVolume, nPan, lpBuffer);
		}
	};
}

TEST_CASE_FIXTURE(PlayerFixture, "Begin opens the file through Storm and starts the sound")
{
	HANDLE hFile = FakeStorm::AddFile("not really audio, but the fake engine doesn't mind");

	REQUIRE(Begin(hFile));

	std::shared_ptr<FakeSoundState> sound = engine.Last();
	REQUIRE(sound.get());
	CHECK(std::string(sound->fileContents.begin(), sound->fileContents.end()) == "not really audio, but the fake engine doesn't mind");
	CHECK(sound->bStarted);
	CHECK_FALSE(sound->bLoop);
	CHECK(sound->nSeekedTo == UINT64_MAX);

	SUBCASE("Default volume and pan leave the sound alone")
	{
		CHECK(sound->nVolume == doctest::Approx(1.0f));
		CHECK(sound->nPan == 0.0f);
		CHECK(sound->nPitch == 1.0f);
		CHECK_FALSE(sound->bMuted);
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Begin reads the file from the start even if the game already read some of it")
{
	HANDLE hFile = FakeStorm::AddFile("RIFF header and data");
	char header[4];
	DWORD nRead;
	FakeStorm::Functions().SFileReadFile(hFile, header, sizeof(header), &nRead, NULL);

	REQUIRE(Begin(hFile));
	CHECK(engine.Last()->fileContents.size() == 20);
}

TEST_CASE_FIXTURE(PlayerFixture, "The looping flag is passed to the engine")
{
	REQUIRE(Begin(FakeStorm::AddFile("x"), SFILE_DDA_LOOPING));
	CHECK(engine.Last()->bLoop);
}

TEST_CASE_FIXTURE(PlayerFixture, "Begin fails cleanly if the engine can't decode the file")
{
	engine.bCanDecode = false;
	HANDLE hFile = FakeStorm::AddFile("garbage");

	CHECK_FALSE(Begin(hFile));

	// Nothing is left behind: the handle isn't playing, and closing it closes it right away
	CHECK_FALSE(player.GetVolume(hFile, NULL, NULL));
	CHECK_FALSE(player.End(hFile));
	CHECK(player.CloseFile(hFile));
	CHECK(FakeStorm::IsClosed(hFile));
}

TEST_CASE_FIXTURE(PlayerFixture, "Begin rejects null handles and empty buffers, as Storm does")
{
	HANDLE hFile = FakeStorm::AddFile("x");

	CHECK_FALSE(player.Begin(NULL, 0, 0x10000, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL));
	CHECK_FALSE(player.Begin(hFile, 0, 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, NULL));
	CHECK(engine.sounds.empty());
}

TEST_CASE_FIXTURE(PlayerFixture, "The start position is converted from bytes of 16-bit PCM to frames")
{
	SUBCASE("Stereo")
	{
		REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 4000));
		CHECK(engine.Last()->nSeekedTo == 1000);
	}

	SUBCASE("Mono")
	{
		engine.nChannels = 1;
		REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 4000));
		CHECK(engine.Last()->nSeekedTo == 2000);
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Initial volume and pan are converted")
{
	REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 0, -2000, 600));

	std::shared_ptr<FakeSoundState> sound = engine.Last();
	CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-2000)));
	CHECK(sound->nPan == doctest::Approx(DdaPanToLinear(600)));
}

TEST_CASE_FIXTURE(PlayerFixture, "Ending a sound stops it")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));
	std::shared_ptr<FakeSoundState> sound = engine.Last();

	CHECK(player.End(hFile));
	CHECK(sound->bDestroyed);

	SUBCASE("A second End fails")
	{
		CHECK_FALSE(player.End(hFile));
	}

	SUBCASE("End on something that never played fails")
	{
		CHECK_FALSE(player.End(FakeStorm::AddFile("y")));
		CHECK_FALSE(player.End(NULL));
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Beginning a sound that is already playing restarts it")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));
	std::shared_ptr<FakeSoundState> first = engine.Last();

	REQUIRE(Begin(hFile));
	std::shared_ptr<FakeSoundState> second = engine.Last();

	CHECK(first.get() != second.get());
	CHECK(first->bDestroyed);
	CHECK_FALSE(second->bDestroyed);
	CHECK(second->bStarted);
}

TEST_CASE_FIXTURE(PlayerFixture, "Closing a playing file is deferred until the sound ends")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));

	CHECK(player.CloseFile(hFile));
	CHECK_FALSE(FakeStorm::IsClosed(hFile));

	SUBCASE("Until SFileDdaEnd")
	{
		CHECK(player.End(hFile));
		CHECK(FakeStorm::CloseCount(hFile) == 1);
	}

	SUBCASE("Even after the sound has finished on its own")
	{
		engine.Last()->bPlaying = false;
		player.Update(TRUE);
		CHECK_FALSE(FakeStorm::IsClosed(hFile));

		CHECK(player.End(hFile));
		CHECK(FakeStorm::CloseCount(hFile) == 1);
	}

	SUBCASE("Until StopAll, as in SFileDdaDestroy")
	{
		player.StopAll();
		CHECK(FakeStorm::CloseCount(hFile) == 1);
	}

	SUBCASE("Restarting a file the game has closed still closes it once")
	{
		REQUIRE(Begin(hFile));
		CHECK(FakeStorm::CloseCount(hFile) == 1);
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Closing a file that isn't playing closes it immediately")
{
	HANDLE hFile = FakeStorm::AddFile("x");

	CHECK(player.CloseFile(hFile));
	CHECK(FakeStorm::CloseCount(hFile) == 1);

	SUBCASE("Also once its sound has ended")
	{
		HANDLE hOther = FakeStorm::AddFile("y");
		REQUIRE(Begin(hOther));
		REQUIRE(player.End(hOther));

		CHECK(player.CloseFile(hOther));
		CHECK(FakeStorm::CloseCount(hOther) == 1);
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "GetPos reports positions in bytes of 16-bit PCM")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));
	std::shared_ptr<FakeSoundState> sound = engine.Last();
	sound->nCursor = 1000;
	sound->nLength = 44100;

	DWORD nPos = 0, nLength = 0;
	REQUIRE(player.GetPos(hFile, &nPos, &nLength));
	CHECK(nPos == 4000);
	CHECK(nLength == 176400);

	SUBCASE("Either output may be omitted")
	{
		CHECK(player.GetPos(hFile, NULL, &nLength));
		CHECK(player.GetPos(hFile, &nPos, NULL));
		CHECK(player.GetPos(hFile, NULL, NULL));
	}

	SUBCASE("An unknown length is reported as just past the position")
	{
		sound->bLengthKnown = false;
		REQUIRE(player.GetPos(hFile, &nPos, &nLength));
		CHECK(nPos == 4000);
		CHECK(nLength == 4004);
	}

	SUBCASE("The position is always less than the length, even if the length was an estimate")
	{
		sound->nCursor = 50000;
		REQUIRE(player.GetPos(hFile, &nPos, &nLength));
		CHECK(nPos == 200000);
		CHECK(nLength > nPos);
	}

	SUBCASE("It fails once the sound has finished")
	{
		sound->bPlaying = false;
		CHECK_FALSE(player.GetPos(hFile, &nPos, &nLength));
	}

	SUBCASE("It fails for files that aren't playing")
	{
		CHECK_FALSE(player.GetPos(FakeStorm::AddFile("y"), &nPos, &nLength));
		CHECK_FALSE(player.GetPos(NULL, &nPos, &nLength));
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Volume and pan can be read back and changed")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile, 0, 0, -1000, -500));
	std::shared_ptr<FakeSoundState> sound = engine.Last();

	long nVolume = 1, nPan = 1;
	REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
	CHECK(nVolume == -1000);
	CHECK(nPan == -500);

	SUBCASE("Setting both")
	{
		REQUIRE(player.SetVolume(hFile, -3000, 2000));
		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-3000)));
		CHECK(sound->nPan == doctest::Approx(DdaPanToLinear(2000)));

		REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == -3000);
		CHECK(nPan == 2000);
	}

	SUBCASE("SFILE_DDA_DEFAULT leaves a setting unchanged")
	{
		REQUIRE(player.SetVolume(hFile, SFILE_DDA_DEFAULT, 2000));
		REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == -1000);
		CHECK(nPan == 2000);

		REQUIRE(player.SetVolume(hFile, -4000, SFILE_DDA_DEFAULT));
		REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == -4000);
		CHECK(nPan == 2000);
	}

	SUBCASE("Both work after the sound has finished, as in Storm")
	{
		sound->bPlaying = false;
		player.Update(TRUE);

		CHECK(player.SetVolume(hFile, -2500, SFILE_DDA_DEFAULT));
		REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == -2500);
	}

	SUBCASE("Both fail for files that aren't playing")
	{
		HANDLE hOther = FakeStorm::AddFile("y");
		CHECK_FALSE(player.GetVolume(hOther, &nVolume, &nPan));
		CHECK_FALSE(player.SetVolume(hOther, 0, 0));
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Defaults are reported as full volume, centered")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));

	long nVolume = 1, nPan = 1;
	REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
	CHECK(nVolume == DSBVOLUME_MAX);
	CHECK(nPan == DSBPAN_CENTER);
}

TEST_CASE_FIXTURE(PlayerFixture, "Sounds that finish are noticed by Update")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));
	std::shared_ptr<FakeSoundState> sound = engine.Last();

	sound->bPlaying = false;
	player.Update(TRUE);

	// The sound is still there until SFileDdaEnd; only its state changed
	CHECK_FALSE(sound->bDestroyed);
	CHECK(player.End(hFile));
}

TEST_CASE_FIXTURE(PlayerFixture, "Non-global sounds are muted while the game is in the background")
{
	HANDLE hFile = FakeStorm::AddFile("x");
	REQUIRE(Begin(hFile));
	std::shared_ptr<FakeSoundState> sound = engine.Last();

	player.Update(FALSE);
	CHECK(sound->bMuted);

	player.Update(FALSE);
	CHECK(sound->bMuted);

	player.Update(TRUE);
	CHECK_FALSE(sound->bMuted);

	SUBCASE("Sounds started in the background start muted")
	{
		player.Update(FALSE);
		HANDLE hOther = FakeStorm::AddFile("y");
		REQUIRE(Begin(hOther));
		CHECK(engine.Last()->bMuted);

		player.Update(TRUE);
		CHECK_FALSE(engine.Last()->bMuted);
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Sounds with global buffers keep playing in the background")
{
	FakeDirectSoundBuffer &buffer = NewBuffer();
	buffer.dwCapsFlags |= DSBCAPS_GLOBALFOCUS;

	REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &buffer));
	std::shared_ptr<FakeSoundState> sound = engine.Last();

	player.Update(FALSE);
	CHECK_FALSE(sound->bMuted);

	SUBCASE("Also when started in the background")
	{
		FakeDirectSoundBuffer &other = NewBuffer();
		other.dwCapsFlags |= DSBCAPS_GLOBALFOCUS;
		REQUIRE(Begin(FakeStorm::AddFile("y"), 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &other));
		CHECK_FALSE(engine.Last()->bMuted);
		player.StopAll();
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "StopAll ends every sound")
{
	HANDLE hFiles[3] = { FakeStorm::AddFile("a"), FakeStorm::AddFile("b"), FakeStorm::AddFile("c") };
	for (HANDLE hFile : hFiles)
		REQUIRE(Begin(hFile));

	player.StopAll();

	for (std::shared_ptr<FakeSoundState> &sound : engine.sounds)
		CHECK(sound->bDestroyed);
	for (HANDLE hFile : hFiles)
		CHECK_FALSE(player.End(hFile));
}

TEST_CASE_FIXTURE(PlayerFixture, "Game-supplied buffers stand in for the sound")
{
	FakeDirectSoundBuffer &buffer = NewBuffer(22050, 16, 2);
	buffer.nVolume = -1500;
	buffer.nPan = 300;
	buffer.bPlaying = true;
	HANDLE hFile = FakeStorm::AddFile("x");

	REQUIRE(Begin(hFile, 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &buffer));
	std::shared_ptr<FakeSoundState> sound = engine.Last();

	SUBCASE("The buffer is silenced, so the game can't play junk through it")
	{
		CHECK(buffer.IsSilent());
		CHECK_FALSE(buffer.bLocked);
	}

	SUBCASE("The buffer plays, looping, so the game sees the sound is playing")
	{
		CHECK(buffer.bPlaying);
		CHECK(buffer.bLooping);

		DWORD dwStatus = 0;
		buffer.GetStatus(&dwStatus);
		CHECK((dwStatus & DSBSTATUS_PLAYING) != 0);
	}

	SUBCASE("We hold a reference to the buffer while the sound plays")
	{
		CHECK(buffer.nRefCount == 2);
		REQUIRE(player.End(hFile));
		CHECK(buffer.nRefCount == 1);
		CHECK_FALSE(buffer.bPlaying);
	}

	SUBCASE("The sound adopts the buffer's volume and pan")
	{
		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-1500)));
		CHECK(sound->nPan == doctest::Approx(DdaPanToLinear(300)));
	}

	SUBCASE("When the sound finishes, the buffer stops, so the game sees it (Diablo II dialogue)")
	{
		sound->bPlaying = false;
		player.Update(TRUE);

		DWORD dwStatus = 0;
		buffer.GetStatus(&dwStatus);
		CHECK((dwStatus & DSBSTATUS_PLAYING) == 0);
	}

	SUBCASE("Changes the game makes to the buffer are copied to the sound")
	{
		buffer.SetVolume(-2500);
		buffer.SetPan(-800);
		buffer.SetFrequency(44100);
		player.Update(TRUE);

		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-2500)));
		CHECK(sound->nPan == doctest::Approx(DdaPanToLinear(-800)));
		CHECK(sound->nPitch == doctest::Approx(2.0f));

		long nVolume, nPan;
		REQUIRE(player.GetVolume(hFile, &nVolume, &nPan));
		CHECK(nVolume == -2500);
		CHECK(nPan == -800);
	}

	SUBCASE("SFileDdaSetVolume also updates the buffer, so Update doesn't undo it")
	{
		REQUIRE(player.SetVolume(hFile, -400, 900));
		CHECK(buffer.nVolume == -400);
		CHECK(buffer.nPan == 900);

		player.Update(TRUE);
		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-400)));
		CHECK(sound->nPan == doctest::Approx(DdaPanToLinear(900)));
	}

	SUBCASE("Nothing is copied from the buffer after the sound finished")
	{
		sound->bPlaying = false;
		player.Update(TRUE);
		buffer.SetVolume(-5000);
		player.Update(TRUE);
		CHECK(sound->nVolume == doctest::Approx(DdaVolumeToLinear(-1500)));
	}
}

TEST_CASE_FIXTURE(PlayerFixture, "Volume and pan passed with a game-supplied buffer are set on the buffer")
{
	FakeDirectSoundBuffer &buffer = NewBuffer();
	REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 0, -700, -300, &buffer));

	CHECK(buffer.nVolume == -700);
	CHECK(buffer.nPan == -300);
	CHECK(engine.Last()->nVolume == doctest::Approx(DdaVolumeToLinear(-700)));
}

TEST_CASE_FIXTURE(PlayerFixture, "Frequencies of game-supplied buffers are relative to the buffer's rate")
{
	// Diablo II assumes 22050 hz, whatever the sound's own rate
	FakeDirectSoundBuffer &buffer = NewBuffer(22050);
	buffer.nFrequency = 11025;
	engine.nSampleRate = 44100;

	REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &buffer));
	CHECK(engine.Last()->nPitch == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(PlayerFixture, "8-bit game-supplied buffers are silenced with 0x80")
{
	FakeDirectSoundBuffer &buffer = NewBuffer(22050, 8, 1);
	REQUIRE(Begin(FakeStorm::AddFile("x"), 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &buffer));
	CHECK(buffer.data[0] == 0x80);
	CHECK(buffer.IsSilent());
}

TEST_CASE_FIXTURE(PlayerFixture, "A buffer that is replaced by restarting the sound is released")
{
	FakeDirectSoundBuffer &first = NewBuffer();
	FakeDirectSoundBuffer &second = NewBuffer();
	HANDLE hFile = FakeStorm::AddFile("x");

	REQUIRE(Begin(hFile, 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &first));
	REQUIRE(Begin(hFile, 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &second));

	CHECK(first.nRefCount == 1);
	CHECK_FALSE(first.bPlaying);
	CHECK(second.nRefCount == 2);
}

TEST_CASE_FIXTURE(PlayerFixture, "A failed Begin doesn't keep a reference to the buffer")
{
	engine.bCanDecode = false;
	FakeDirectSoundBuffer &buffer = NewBuffer();

	CHECK_FALSE(Begin(FakeStorm::AddFile("x"), 0, 0, SFILE_DDA_DEFAULT, SFILE_DDA_DEFAULT, &buffer));
	CHECK(buffer.nRefCount == 1);
	CHECK_FALSE(buffer.bPlaying);
}

TEST_CASE_FIXTURE(PlayerFixture, "The player can be used from several threads at once")
{
	const int nThreads = 4, nIterations = 200;
	std::vector<HANDLE> files;
	for (int i = 0; i < nThreads; i++)
		files.push_back(FakeStorm::AddFile("x"));

	volatile bool bStop = false;
	std::thread updater([&]() {
		BOOL bActivated = TRUE;
		while (!bStop)
		{
			player.Update(bActivated);
			bActivated = !bActivated;
		}
	});

	std::vector<std::thread> workers;
	for (int iThread = 0; iThread < nThreads; iThread++)
	{
		workers.emplace_back([&, iThread]() {
			HANDLE hFile = files[iThread];
			for (int i = 0; i < nIterations; i++)
			{
				player.Begin(hFile, SFILE_DDA_LOOPING, 0x10000, 0, -i, i, NULL);

				DWORD nPos, nLength;
				long nVolume, nPan;
				player.GetPos(hFile, &nPos, &nLength);
				player.SetVolume(hFile, -i, -i);
				player.GetVolume(hFile, &nVolume, &nPan);

				if (i % 3 == 0)
					player.CloseFile(hFile);
				if (i % 2 == 0)
					player.End(hFile);
			}
		});
	}

	for (std::thread &worker : workers)
		worker.join();
	bStop = true;
	updater.join();

	player.StopAll();

	// Every sound that was opened has been destroyed, and nothing is still playing
	for (std::shared_ptr<FakeSoundState> &sound : engine.sounds)
		CHECK(sound->bDestroyed);
	CHECK(engine.sounds.size() == (size_t)nThreads * nIterations);
	for (HANDLE hFile : files)
		CHECK_FALSE(player.GetVolume(hFile, NULL, NULL));
}
