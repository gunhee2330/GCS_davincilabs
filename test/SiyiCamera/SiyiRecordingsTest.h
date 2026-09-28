#pragma once

#include "UnitTest.h"

/// Tests SiyiRecordings, what the 영상 기록 page reads, against a fake pod media API on loopback.
class SiyiRecordingsTest : public UnitTest
{
    Q_OBJECT

protected slots:
    /// Starts from no copies, index or recording log: they live outside the per-test settings reset.
    void init() override;

private slots:
    /// The RFP p9 names, yyyyMMddhhmmsszzz.mp4 and yyyyMMddhhmmsszzz_lat_lon.mp4 to four decimals;
    /// the time a pod file's name carries, if any; and the logged start a pod file is matched to:
    /// the nearest to a start the pod names, within the clock slack, and the last one before a
    /// file's last write.
    void _namesAndMatching_test();

    /// Recording starts are logged with the aircraft's position only on an off-to-on change seen
    /// on a connected link: the first report after (re)connecting is only the baseline. The log
    /// outlives the instance.
    void _recordingLog_test();

    /// The pod's folders and videos are listed, a file with a time in its name dated by it and one
    /// without by its last write. A download goes 0 to 100 percent and lands under the RFP name,
    /// with position when a logged start matches and the setting asks for it, without in the
    /// other form or when nothing matches (the pod's time is used then, and said so). The copy is
    /// byte for byte the pod's and its length is read from the MP4. It can be exported and deleted.
    void _listsAndDownloads_test();

    /// A download under way stops on cancel, leaves no partial file and nothing received.
    void _cancel_test();

    /// Copies older than the auto delete days go at purge, newer ones stay, and 끄기 keeps all.
    void _autoDelete_test();
};
