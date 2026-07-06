// --- START OF FILE: HexaStudio/viewport3d/tests/gtest_urdf_visual_model.cpp ---
// Unit tests for the pure URDF -> visual-chain loader. These pin the exact behaviours that produced
// real defects in the viewport: the terminal flange frame must not be dropped, the authored URDF
// colours must survive into the visual model, and mesh URIs must resolve to real files.
#include <gtest/gtest.h>

#include <QFileInfo>

#include "UrdfVisualModel.h"

using hexa::UrdfVisualModel;
using hexa::loadUrdfVisualModel;

namespace {
const QString kUrdfPath = QString::fromUtf8(VIEWPORT3D_TEST_URDF);
const QString kBaseLink = QStringLiteral("base_link");
} // namespace

TEST(UrdfVisualModelTest, LoadsProductionArm) {
    const UrdfVisualModel model = loadUrdfVisualModel(kUrdfPath, kBaseLink);
    ASSERT_TRUE(model.valid) << model.error.toStdString();
    ASSERT_EQ(model.axes.size(), UrdfVisualModel::kAxisCount);

    // Every axis link resolves to an existing mesh file.
    EXPECT_TRUE(QFileInfo::exists(model.base.meshPath)) << model.base.meshPath.toStdString();
    for (const auto& axis : model.axes) {
        EXPECT_TRUE(QFileInfo::exists(axis.visual.meshPath)) << axis.visual.meshPath.toStdString();
    }
}

TEST(UrdfVisualModelTest, CapturesTerminalFlangeFrame) {
    const UrdfVisualModel model = loadUrdfVisualModel(kUrdfPath, kBaseLink);
    ASSERT_TRUE(model.valid) << model.error.toStdString();

    // The bench URDF defines a bare 30 mm flange frame along -Z of Link_6 (Joint_Flange).
    EXPECT_TRUE(model.hasFlange);
    EXPECT_EQ(model.flangeLinkName, QStringLiteral("flange"));
    EXPECT_NEAR(model.flangeOriginMm.x(), 0.0f, 1e-3f);
    EXPECT_NEAR(model.flangeOriginMm.y(), 0.0f, 1e-3f);
    EXPECT_NEAR(model.flangeOriginMm.z(), -30.0f, 1e-3f);
    EXPECT_TRUE(model.flangeVisual.meshPath.isEmpty());   // bare frame: no mesh
}

TEST(UrdfVisualModelTest, PropagatesAuthoredLinkColors) {
    const UrdfVisualModel model = loadUrdfVisualModel(kUrdfPath, kBaseLink);
    ASSERT_TRUE(model.valid) << model.error.toStdString();

    // The URDF authors dark grey (0.098...) for base/Link_1..4 and lighter grey (0.439...) for
    // Link_5/Link_6. The loader must deliver the authored values, not a hardcode.
    EXPECT_NEAR(model.base.color.redF(), 0.098, 0.01);
    EXPECT_NEAR(model.axes[0].visual.color.redF(), 0.098, 0.01);   // Link_1
    EXPECT_NEAR(model.axes[4].visual.color.redF(), 0.439, 0.01);   // Link_5
    EXPECT_NEAR(model.axes[5].visual.color.redF(), 0.439, 0.01);   // Link_6
}

TEST(UrdfVisualModelTest, FailsHonestlyOnMissingFile) {
    const UrdfVisualModel model =
        loadUrdfVisualModel(QStringLiteral("Z:/no/such/robot.urdf"), kBaseLink);
    EXPECT_FALSE(model.valid);
    EXPECT_FALSE(model.error.isEmpty());
}
