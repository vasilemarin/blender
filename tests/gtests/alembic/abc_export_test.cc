#include "testing/testing.h"

// Keep first since utildefines defines AT which conflicts with STL
#include "intern/abc_util.h"
#include "intern/export/abc_archive.h"

extern "C" {
#include "BKE_main.h"
#include "BLI_fileops.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "DNA_scene_types.h"
}

#include "DEG_depsgraph.h"

using namespace ABC;

class AlembicExportTest : public testing::Test {
 protected:
  ABCArchive *abc_archive;

  AlembicExportParams params;
  Scene scene;
  Depsgraph *depsgraph;
  Main *bmain;

  virtual void SetUp()
  {
    abc_archive = nullptr;

    params.frame_start = 31.0;
    params.frame_end = 223.0;

    /* Fake a 25 FPS scene with a nonzero base (because that's sometimes forgotten) */
    scene.r.frs_sec = 50;
    scene.r.frs_sec_base = 2;
    strcpy(scene.id.name, "SCTestScene");

    bmain = BKE_main_new();

    /* TODO(sergey): Pass scene layer somehow? */
    ViewLayer *view_layer = (ViewLayer *)scene.view_layers.first;
    depsgraph = DEG_graph_new(bmain, &scene, view_layer, DAG_EVAL_RENDER);
  }

  virtual void TearDown()
  {
    BKE_main_free(bmain);
    DEG_graph_free(depsgraph);
    deleteArchive();
  }

  // Call after setting up the parameters.
  void createArchive()
  {
    if (abc_archive != nullptr) {
      deleteArchive();
    }
    abc_archive = new ABCArchive(bmain, &scene, params, "somefile.abc");
  }

  void deleteArchive()
  {
    delete abc_archive;
    if (BLI_exists("somefile.abc")) {
      BLI_delete("somefile.abc", false, false);
    }
    abc_archive = nullptr;
  }
};

TEST_F(AlembicExportTest, TimeSamplesFullShutter)
{
  params.shutter_open = 0.0;
  params.shutter_close = 1.0;
  params.frame_start = 31.0;
  params.frame_end = 32.0;

  /* test 5 samples per frame */
  params.frame_samples_xform = params.frame_samples_shape = 5;
  createArchive();
  std::vector<double> frames(abc_archive->frames_begin(), abc_archive->frames_end());
  EXPECT_EQ(5, frames.size());
  EXPECT_NEAR(31.0, frames[0], 1e-5f);
  EXPECT_NEAR(31.2, frames[1], 1e-5f);
  EXPECT_NEAR(31.4, frames[2], 1e-5f);
  EXPECT_NEAR(31.6, frames[3], 1e-5f);
  EXPECT_NEAR(31.8, frames[4], 1e-5f);
}

// TEST_F(AlembicExportTest, TimeSamples180degShutter)
// {
//   params.shutter_open = -0.25;
//   params.shutter_close = 0.25;

//   createArchive();
//   std::vector<double> samples;

//   /* test 5 samples per frame */
//   exporter->getShutterSamples(5, true, samples);
//   EXPECT_EQ(5, samples.size());
//   EXPECT_NEAR(1.230, samples[0], 1e-5f);
//   EXPECT_NEAR(1.234, samples[1], 1e-5f);
//   EXPECT_NEAR(1.238, samples[2], 1e-5f);
//   EXPECT_NEAR(1.242, samples[3], 1e-5f);
//   EXPECT_NEAR(1.246, samples[4], 1e-5f);

//   /* test same, but using frame number offset instead of time */
//   exporter->getShutterSamples(5, false, samples);
//   EXPECT_EQ(5, samples.size());
//   EXPECT_NEAR(-0.25, samples[0], 1e-5f);
//   EXPECT_NEAR(-0.15, samples[1], 1e-5f);
//   EXPECT_NEAR(-0.05, samples[2], 1e-5f);
//   EXPECT_NEAR(0.05, samples[3], 1e-5f);
//   EXPECT_NEAR(0.15, samples[4], 1e-5f);

//   /* Use the same setup to test getFrameSet().
//    * Here only a few numbers are tested, due to rounding issues. */
//   std::set<double> frames;
//   exporter->getFrameSet(5, frames);
//   EXPECT_EQ(965, frames.size());
//   EXPECT_EQ(1, frames.count(30.75));
//   EXPECT_EQ(1, frames.count(30.95));
//   EXPECT_EQ(1, frames.count(31.15));
// }
