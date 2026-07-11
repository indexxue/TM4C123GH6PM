# x-io Fusion (vendored)

Sensor fusion library for IMU/AHRS by [x-io Technologies](https://github.com/xioTechnologies/Fusion).

- **License**: MIT — see [LICENSE.md](LICENSE.md)
- **Upstream**: https://github.com/xioTechnologies/Fusion
- **Used by**: `Common/src/attitude.c`

Sources under `Fusion/` are copied from upstream `main` (FusionAhrs, FusionBias, FusionMath, FusionRemap, FusionConvention).

To refresh (when network allows):

```powershell
$base = "https://raw.githubusercontent.com/xioTechnologies/Fusion/main/Fusion"
$dest = "third_party/Fusion/Fusion"
# download FusionAhrs.c, FusionAhrs.h, FusionBias.c, FusionBias.h, FusionConvention.h, FusionMath.h, FusionRemap.h
```
