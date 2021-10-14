// IZebraService.aidl
package android.zebra;

// Declare any non-default types here with import statements
interface IZebraService {
    void getConnectStatus(out int[] clients);
}
