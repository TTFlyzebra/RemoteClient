// IZebraService.aidl
package android.zebra;

// Declare any non-default types here with import statements
interface IZebraService {
    void getConnecetStatus(out int[] clients);
}
